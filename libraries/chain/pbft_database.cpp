/**
 *  @file
 *  @copyright defined in bos/LICENSE
 */



#include <eosio/chain/controller.hpp>

namespace eosio { namespace chain {
   
      pbft_database::pbft_database( controller& c ): ctrl( c ) {
         
         // init ppcm_state_index and current_view from file, then delete file
         auto ppcm_db_dir = ctrl.state_dir;
         if( !fc::is_directory( ppcm_db_dir ))
            fc::create_directories( ppcm_db_dir );

         auto ppcm_db_dat = ppcm_db_dir / config::pbft_ppcm_db_filename;
         if( fc::exists( ppcm_db_dat )){
            string content;
            fc::read_file_contents(ppcm_db_dat, content);
            fc::datastream<const char *> ds( content.data(), content.size() );

            unsigned_int size;
            fc::raw::unpack(ds, size);
            for( uint32_t i = 0, n = size.value; i < n; ++i ){
               pbft_ppcm_state s;
               fc::raw::unpack(ds, s);
               auto state_ptr = std::make_shared<pbft_ppcm_state>( std::move(s) );
               auto ret = ppcm_state_index.insert( state_ptr );
               if( ! ret.second )
                  elog( "unable to insert pbft_ppcm_state" );
            }

            fc::raw::unpack(ds, current_view);
            
            fc::remove( ppcm_db_dat );
         } else {
            ppcm_state_index  = pbft_ppcm_state_multi_index_type{};
            current_view      = 0;
         }

         // init checkpoint_index from file, then delete file
         auto checkpoints_dir = ctrl.blocks_dir;
         if( !fc::is_directory( checkpoints_dir )) 
            fc::create_directories( checkpoints_dir );

         auto checkpoints_db_dat = checkpoints_dir / config::pbft_checkpoints_db_filename;
         if( fc::exists( checkpoints_db_dat )){
            string content;
            fc::read_file_contents(checkpoints_db_dat_dat, content);
            fc::datastream<const char *> ds(content.data(), content.size());

            unsigned_int size;
            fc::raw::unpack(ds, size);
            for (uint32_t i = 0, n = size.value; i < n; ++i) {
               pbft_checkpoint_state s;
               fc::raw::unpack(ds, s);
               
               auto state_ptr = std::make_shared<pbft_checkpoint_state>( std::move(s) );
               auto ret = checkpoint_index.insert( state_ptr );
               if( ! ret.second )
                  elog( "unable to insert pbft_checkpoint_state" );
            }

            fc::remove( checkpoints_db_dat );
         } else {
            checkpoint_index = pbft_checkpoint_state_multi_index_type{};
         }
         
         // init other members
         view_state_index = pbft_view_state_multi_index_type{};
         prepare_watermarks = vector<uint32_t>{};
      }
      
      void pbft_database::close() {
         // pack ppcm_state_index and current_view to file
         fc::path ppcm_db_dat = ppcm_db_dir / config::pbft_ppcm_db_filename;
         std::ofstream out( ppcm_db_dat.generic_string().c_str(), std::ios::out | std::ios::binary | std::ofstream::app );
         uint32_t size = ppcm_state_index.size();
         fc::raw::pack( out, unsigned_int{size} );
         for( const auto& state_ptr : ppcm_state_index ){
            fc::raw::pack( out, *state_ptr );
         }
         fc::raw::pack( out, current_view );
         ppcm_state_index.clear();

         // pack checkpoint_index to file
         fc::path checkpoints_db_dat = checkpoints_dir / config::pbft_checkpoints_db_filename;
         std::ofstream c_out( checkpoints_db_dat.generic_string().c_str(), std::ios::out | std::ios::binary | std::ofstream::trunc );

         uint32_t num_records_in_checkpoint_db = checkpoint_index.size();
         fc::raw::pack(c_out, unsigned_int{num_records_in_checkpoint_db});

         for (const auto &s: checkpoint_index) {
            fc::raw::pack(c_out, *s);
         }
         checkpoint_index.clear();
      }

      pbft_database::~pbft_database() {
         close();
      }

      bool pbft_database::is_valid_prepare( const pbft_prepare& prepare ) {
         auto block_num = block_header::num_from_id( prepare.block_id );
         return block_num > ctrl.last_stable_checkpoint_block_num() && prepare.is_signature_valid() && 
                should_recv_pbft_msg( prepare.public_key );
      }

      bool pbft_database::is_valid_commit( const pbft_commit& commit ) {
         auto block_num = block_header::num_from_id( commit.block_id );
         return block_num > ctrl.last_stable_checkpoint_block_num() && commit.is_signature_valid() &&
                should_recv_pbft_msg( commit.public_key );
      }

      bool pbft_database::is_valid_checkpoint( const pbft_checkpoint& checkpoint ) {
         auto block_num = block_header::num_from_id( checkpoint.block_id );
         if ( block_num > ctrl.head_block_num() || block_num <= ctrl.last_stable_checkpoint_block_num() || 
              ! cp.is_signature_valid() ) return false;
         
         auto bsp = ctrl.fetch_block_state_by_id( checkpoint.block_id );
         if ( bsp ) {
            auto active_producers = bs->active_schedule.producers;
            for (const auto& producer : active_producers) {
               if ( producer.block_signing_key == checkpoint.public_key) 
                  return true;
            }
         }
         return false;
      }

      bool pbft_database::is_valid_stable_checkpoint( const pbft_stable_checkpoint& stable_checkpoint ) {
         auto block_num = block_header::num_from_id( stable_checkpoint.block_id );
         
         if ( block_num <= ctrl.last_stable_checkpoint_block_num() )
            return false;
         
         for (const auto& checkpoint : stable_checkpoint.checkpoints) {
            auto valid = is_valid_checkpoint( checkpoint ) && checkpoint.block_id == stable_checkpoint.block_id;
            if ( ! valid ) return false;
         }

         auto bsp = ctrl.fetch_block_state_by_number( block_num );
         if ( ! bsp ) return false;

         const auto& active_producers = bsp->active_schedule.producers;
         const auto& checkpoints = stable_checkpoint.checkpoints;
         if ( checkpoints.size() < as.producers.size() * 2 / 3 + 1 )
            return false;

         auto cp_count = 0;
         for (const auto& producer : active_producers ) {
            for (const auto& checkpoint : stable_checkpoint.checkpoints ) {
               if ( producer.block_signing_key == checkpoint.public_key )
                  cp_count += 1;
            }
         }

         return cp_count >= as.producers.size() * 2 / 3 + 1;
      }

      bool pbft_database::is_valid_view_change( const pbft_view_change& view_change ) {
         return view_change.is_signature_valid() && should_recv_pbft_msg( view_change.public_key );
      }

      bool pbft_database::is_valid_new_view( const pbft_new_view& new_view ) {

         EOS_ASSERT( new_view.is_signature_valid(), pbft_exception, "bad new view signature");
         EOS_ASSERT( new_view.public_key == get_new_view_primary_key(new_view.view), pbft_exception, "new view is not signed with expected key");
         EOS_ASSERT( is_valid_prepared_certificate(new_view.prepared), pbft_exception, "bad prepared certificate: ${pc}", ("pc", new_view.prepared));
         EOS_ASSERT( is_valid_stable_checkpoint(new_view.stable_checkpoint), pbft_exception, "bad stable checkpoint: ${scp}", ("scp", new_view.stable_checkpoint));

         for ( const auto& committed : new_view.committeds) {
            EOS_ASSERT( is_valid_committed_certificate(committed), pbft_exception, "bad committed certificate: ${cc}", ("cc", c));
         }

         EOS_ASSERT( new_view.view_changed.is_signature_valid(), pbft_exception, "bad view changed signature");
         EOS_ASSERT( new_view.view_changed.view == new_view.view, pbft_exception, "target view not match");

         vector<public_key_type> lscb_producers;
         for ( const auto& producer: lscb_active_producers().producers ) {
            lscb_producers.emplace_back( producer.block_signing_key );
         }
         auto schedule_threshold = lscb_producers.size() * 2 / 3 + 1;

         vector<public_key_type> view_change_producers;

         for (auto vc: new_view.view_changed.view_changes) {
            if (is_valid_view_change(vc)) {
               add_pbft_view_change(vc);
               view_change_producers.emplace_back(vc.public_key);
            }
         }

         vector<public_key_type> intersection;

         std::sort(lscb_producers.begin(),lscb_producers.end());
         std::sort(view_change_producers.begin(),view_change_producers.end());
         std::set_intersection(lscb_producers.begin(),lscb_producers.end(),
                               view_change_producers.begin(),view_change_producers.end(),
                               back_inserter(intersection));

         EOS_ASSERT(intersection.size() >= schedule_threshold, pbft_exception, "view changes count not enough");

         EOS_ASSERT(should_new_view(new_view.view), pbft_exception, "should not enter new view: ${nv}", ("nv", new_view.view));

         auto highest_ppc = pbft_prepared_certificate{};
         auto highest_pcc = vector<pbft_committed_certificate>{};
         auto highest_scp = pbft_stable_checkpoint{};

         for (const auto &vc: new_view.view_changed.view_changes) {
            if (vc.prepared.block_num > highest_ppc.block_num
                && is_valid_prepared_certificate(vc.prepared)) {
               highest_ppc = vc.prepared;
            }

            for (auto const &cc: vc.committed) {
               if (is_valid_committed_certificate(cc)) {
                  auto p_itr = find_if(highest_pcc.begin(), highest_pcc.end(),
                                       [&](const pbft_committed_certificate &ext) { return ext.block_id == cc.block_id; });
                  if (p_itr == highest_pcc.end()) highest_pcc.emplace_back(cc);
               }
            }

            if (vc.stable_checkpoint.block_num > highest_scp.block_num
                && is_valid_stable_checkpoint(vc.stable_checkpoint)) {
               highest_scp = vc.stable_checkpoint;
            }
         }

         EOS_ASSERT(highest_ppc == new_view.prepared, pbft_exception,
                    "prepared certificate not match, should be ${hpcc} but ${pc} given",
                    ("hpcc",highest_ppc)("pc", new_view.prepared));

         EOS_ASSERT(highest_pcc == new_view.committed, pbft_exception, "committed certificate not match");

         EOS_ASSERT(highest_scp == new_view.stable_checkpoint, pbft_exception,
                    "stable checkpoint not match, should be ${hscp} but ${scp} given",
                    ("hpcc",highest_scp)("pc", new_view.stable_checkpoint));

         return true;
      }




















      void pbft_database::add_pbft_prepare( pbft_prepare& prepare ){
         if( ! is_valid_prepare( prepare )) return;

         auto& by_block_id_index = ppcm_state_index.get<by_block_id>();
         auto current_bsp = ctrl.fetch_block_state_by_id( prepare.block_id );
         auto active_producers = current_bsp->active_producers.producers;

         // is in active producers

         while( current_bsp && current_bsp->block_num > ctrl.last_irreversible_block_num() ) {
            auto ppcm_state_itr = by_block_id_index.find( current_bsp->id );

            if( ppcm_state_itr == by_block_id_index.end() ){
               try {
                  auto state = pbft_ppcm_state{current_bsp->id, current_bsp->block_num, .prepares={prepare}};
                  auto state_ptr = make_shared<pbft_state>( state );
                  ppcm_state_index.insert( state_ptr );
               } catch (...) {
                  elog( "pbft_prepare insert failure: ${p}", ("p", prepare));
               }
            } else if( (*ppcm_state_itr)->should_prepared ){
               break; // i.e. return
            } else {
               auto prepares = (*ppcm_state_itr)->prepares;

               auto p_itr = find_if(prepares.begin(), prepares.end(),
                  [&](const pbft_prepare& p) { return p.public_key == prepare.public_key && p.view == prepare.view; });

               if( p_itr == prepares.end() ){
                  by_block_id_index.modify( ppcm_state_itr, [&](const pbft_ppcm_state_ptr &ptr) {
                     ptr->prepares.emplace_back( prepare );
                  });
               }
            }

            ppcm_state_itr = by_block_id_index.find( current_bsp->id );
            if( ppcm_state_itr == by_block_id_index.end() ) {
               elog( "block id ${id} cannot be found", ("id", current_bsp->id));
               return;
            }

            auto prepares = (*ppcm_state_itr)->prepares;


            if( ! (*ppcm_state_itr)->should_prepared && prepares.size() >= active_producers.size() * 2 / 3 + 1 ){
               flat_map<uint32_t, uint32_t> prepare_count;

               for( const auto& prepare: prepares ){
                  prepare_count[prepare.view] = 0;
               }

               for( const auto& producer: active_producers ){
                  for (auto const& prepare: prepares) {
                     if( producer.block_signing_key == prepare.public_key ){
                           prepare_count[prepare.view] += 1;
                     }
                  }
               }

               for( const auto& count: prepare_count ){
                  if( count.second >= active_producers.size() * 2 / 3 + 1 ){
                     by_block_id_index.modify( ppcm_state_itr, [&](const pbft_state_ptr &psp) {
                        psp->should_prepared = true;
                     });
                     dlog( "block id ${id} is now prepared at view ${v}", ("id", current_bsp->id)("v", count.first));
                  }
               }
            }

            current_bsp = ctrl.fetch_block_state_by_id( current_bsp->prev() );
         } // while
      }

      void pbft_database::add_pbft_commit( pbft_commit& commit ){
         if( ! is_valid_commit( commit )) return;

         auto &by_block_id_index = ppcm_state_index.get<by_block_id>();
         auto current_bsp = ctrl.fetch_block_state_by_id( commit.block_id );
         auto active_producers = current_bsp->active_producers.producers;

         // is in active producers

         while( current_bsp && current_bsp->block_num > ctrl.last_irreversible_block_num() ){
            auto ppcm_state_itr = by_block_id_index.find( current_bsp->id );

            if ( ppcm_state_itr == by_block_id_index.end() ) {
               try {
                  auto state = pbft_ppcm_state{current_bsp->id, current_bsp->block_num, .commits={commit}};
                  auto state_ptr = make_shared<pbft_state>( state );
                  ppcm_state_index.insert( state_ptr );
               } catch (...) {
                  wlog("commit insertion failure: ${c}", ("c", commit));
               }
            } else if( (*ppcm_state_itr)->should_committed ){
               break; // i.e. return
            } else {
               auto commits = (*ppcm_state_itr)->commits;

               auto p_itr = find_if( commits.begin(), commits.end(),
                  [&](const pbft_commit& c){ return c.public_key == commit.public_key && c.view == commit.view; });

               if( p_itr == commits.end() ){
                  by_block_id_index.modify( ppcm_state_itr, [&](const pbft_state_ptr &psp) {
                     psp->commits.emplace_back( commit );
                  });
               }
            }

            ppcm_state_itr = by_block_id_index.find( current_bsp->id );
            if( ppcm_state_itr == by_block_id_index.end() ) {
               elog( "block id ${id} cannot be found", ("id", current_bsp->id));
               return;
            }

            auto commits = (*ppcm_state_itr)->commits;


            if( ! (*ppcm_state_itr)->should_committed && commits.size() >= active_producers.size() * 2 / 3 + 1 ){
               flat_map<uint32_t, uint32_t> commit_count;

               for( const auto& commit: commits ){
                  prepare_count[commit.view] = 0;
               }

               for( const auto& producer: active_producers ){
                  for (auto const& commit: commits) {
                     if( producer.block_signing_key == commit.public_key ){
                        prepare_count[commit.view] += 1;
                     }
                  }
               }

               for( const auto& count: commit_count ){
                  if( count.second >= active_producers.size() * 2 / 3 + 1 ){
                     by_block_id_index.modify( ppcm_state_itr, [&](const pbft_state_ptr &psp) {
                        psp->should_committed = true;
                     });
                     dlog( "block id ${id} is now committed at view ${v}", ("id", current_bsp->id)("v", count.first));
                  }
               }
            }

            current_bsp = ctrl.fetch_block_state_by_id( current_bsp->prev() );
         } // while
      }

      void pbft_database::add_pbft_view_change( pbft_view_change& view_change ){
         if( ! is_valid_view_change( view_change )) return;

         const auto& active_producers = lscb_active_producers().producers;

         auto itr = find_if( active_producers.begin(), active_producers.end(),
            [&](const producer_key& p){ return p.block_signing_key == view_change.public_key;} );
         if ( itr == active_producers.end() ) return;

         uint32_t target_view = view_change.current_view + 1;
         
         auto& by_view_index = view_state_index.get<by_view>();
         auto view_state_itr = by_view_index.find( target_view );
         
         if( view_state_itr == by_view_index.end() ){
            try {
               auto state = pbft_view_state{ target_view, .view_changes={view_change}};
               auto state_ptr = make_shared<pbft_view_state>( state );
               view_state_index.insert( state_ptr );
            } catch (...) {
               elog( "pbft_view_change insert failure: ${p}", ("p", prepare));
            }
         } else if( (*view_state_itr)->should_view_changed ){
            return;
         } else {
            auto view_changes = (*view_state_itr)->view_changes;
            auto p_itr = find_if( view_changes.begin(), view_changes.end(),
               [&](const pbft_view_change& vc){ return vc.public_key == view_change.public_key;} );
            
            if( p_itr == view_changes.end() ){
               by_view_index.modify( view_state_itr, [&](const pbft_view_state_ptr& ptr) {
                  ptr->view_changes.emplace_back( view_change );
               });
            }
         }

         view_state_itr = by_view_index.find( target_view );
         if (view_state_itr == by_view_index.end()) {
            elog( "target_view ${v} cannot be found", ("v", target_view));
            return;
         }

         const auto& view_changes = (*view_state_itr)->view_changes;
         if( view_changes.size() < active_producers.size() * 2 / 3 + 1 ) return;
         
         auto vc_count = 0;
         for( const auto& producer: active_producers ){
            for(const auto& view_change: view_changes ){
               if( producer.block_signing_key == view_change.public_key )
                  vc_count += 1;
            }
         }

         if( vc_count >= active_producers.size() * 2 / 3 + 1 ){
            by_view_index.modify( view_state_itr, [&](const pbft_view_state_ptr& ptr){
               ptr->should_view_changed = true;
            });
         }
      }

      void pbft_database::add_pbft_checkpoint( pbft_checkpoint& checkpoint ){
         if ( ! is_valid_checkpoint(checkpoint) ) return;

         auto checkpoint_bsp = ctrl.fetch_block_state_by_id( checkpoint.block_id );
         if ( ! checkpoint_bsp ) return;
         
         auto& active_producers = checkpoint_bsp->active_schedule.producers;
//         auto itr = find_if( active_producers.begin(), active_producers.end(),
//            [&](const producer_key& p) { return p.block_signing_key == checkpoint.public_key;} );
//         if ( itr == active_producers.end() ) return;

         auto& by_block_id_index = checkpoint_index.get<by_block_id_index>();
         auto checkpoint_state_itr = by_block_id_index.find( checkpoint.block_id );
         if ( checkpoint_state_itr == by_block_id_index.end() ){
            try {
               auto state = pbft_checkpoint_state{ checkpoint.block_id, checkpoint.block_num, .checkpoints={checkpoint} };
               auto state_ptr = make_shared<pbft_checkpoint_state>( state );
               checkpoint_index.insert( state_ptr );
            } catch (...) {
               elog( "pbft_checkpoint insert failure: ${cp}", ("cp", checkpoint));
            }
         } else if ( (*checkpoint_state_itr)->is_stable ){
            return;
         } else {
            auto& checkpoints = (*checkpoint_state_itr)->checkpoints;
            auto p_itr = find_if(checkpoints.begin(), checkpoints.end(),
               [&](const pbft_checkpoint& cp) { return cp.public_key == checkpoint.public_key; });

            if ( p_itr == checkpoints.end() ){
               by_block_id_index.modify( checkpoint_state_itr, [&](const pbft_checkpoint_state_ptr &ptr ) {
                  ptr->checkpoints.emplace_back( checkpoint );
               });
            }
         }

         checkpoint_state_itr = by_block_id_index.find( checkpoint.block_id );
         if ( checkpoint_state_itr == by_block_id_index.end() ){
            elog( "checkpoint ${cp} cannot be found", ("cp", checkpoint));
            return;
         }
         
         auto& checkpoints = (*checkpoint_state_itr)->checkpoints;
         if( checkpoints.size() < active_producers.size() * 2 / 3 + 1 ) return;

         auto cp_count = 0;
         for (const auto& producer: active_producers) {
            for (const auto& cp: checkpoints) {
               if ( producer.block_signing_key == cp.public_key)
                  cp_count += 1;
            }
         }
         
         if ( cp_count >= active_producers.size() * 2 / 3 + 1 ){
            by_block_id_index.modify( checkpoint_state_itr, [&](const pbft_checkpoint_state_ptr& ptr){
               ptr->is_stable = true;
            });

            auto block_id = (*checkpoint_state_itr)->block_id;
            auto bsp = ctrl.fetch_block_by_id( block_id );
            if ( ! bsp ) return;
            
            for( const auto& item : bsp->block_extensions ){
               if( item.first == static_cast<uint16_t>(block_extension_type::pbft_stable_checkpoint) )
                  return;
            }
            
            auto stable_checkpoint = get_stable_checkpoint_by_id( block_id );
            auto buf_size = fc::raw::pack_size( stable_checkpoint );

            auto buffer = std::make_shared<vector<char>>( buf_size );
            fc::datastream<char*> ds( buffer->data(), buf_size );
            fc::raw::pack( ds, stable_checkpoint );

            bsp->block_extensions.emplace_back();
            auto& extension = blk->block_extensions.back();
            extension.first = static_cast<uint16_t>( block_extension_type::pbft_stable_checkpoint );
            extension.second.resize( buf_size );
            std::copy( buffer->begin(),buffer->end(), extension.second.data() );
         }
      }




      bool pbft_database::should_prepared() {
         const auto& index = ppcm_state_index.get<by_should_prepared_and_block_num>();
         if ( index.begin() == index.end()) return false;

         pbft_ppcm_state_ptr state_ptr = index.front();
         auto current_watermark = get_current_pbft_watermark();

         if ( state_ptr->block_num > current_watermark && current_watermark > 0 ) return false;

         if ( state_ptr->should_prepared && state_ptr->block_num > ctrl.last_irreversible_block_num() ){
            ctrl.set_pbft_prepared( state_ptr->block_id );
            return true;
         }

         return false;
      }

      bool pbft_database::should_committed() {
         const auto &index = ppcm_state_index.get<by_should_commited_and_block_num>();
         if ( index.begin() == index.end()) return false;

         pbft_ppcm_state_ptr state_ptr = index.front();

         auto current_watermark = get_current_pbft_watermark();

         if ( state_ptr->block_num > current_watermark && current_watermark > 0 ) return false;

         return state_ptr->should_committed && state_ptr->block_num > ctrl.last_irreversible_block_num();
      }

      uint32_t pbft_database::should_view_change() {
         auto& by_view_index = view_state_index.get<by_view>();
         auto itr = by_view_index.begin();

         auto active_producers = lscb_active_producers().producers;
         
         while ( itr != by_view_index.end() ){
            auto view_state_ptr = *itr;
            view_changes = view_state_ptr->view_changes;

            if ( view_changes.size() < active_producers.size() / 3 + 1 ) {
               continue;
            }
            
            auto vc_count = 0;
            for ( const auto& producer : active_producers ) {
               for ( const auto& view_change: view_changes ) {
                  if ( producer.block_signing_key == view_change.public_key )
                     vc_count += 1;
               }
            }
            
            //if contains self or view_change >= f+1, transit to view_change and send view change
            if ( vc_count >= active_producers.size() / 3 + 1 ) {
               return view_state_ptr->view;
            }
            ++itr;
         }// while
         return 0;
      }

      bool pbft_database::should_new_view( const uint32_t view_num ) {
         auto& by_view_index = view_state_index.get<by_view>();
         auto itr = by_view_index.find( view_num );
         if ( itr == by_view_index.end() ) return false;
         return (*itr)->should_view_changed;
      }

      bool pbft_database::is_new_primary(const uint32_t target_view) {
         auto primary_key = get_new_view_primary_key(target_view);
         if (primary_key == public_key_type{}) return false;
         auto sps = ctrl.my_signature_providers();
         auto sp_itr = sps.find(primary_key);
         return sp_itr != sps.end();
      }






      vector<pbft_prepare> pbft_database::send_and_add_pbft_prepare( const vector<pbft_prepare>& prepares, pbft_view_type current_view ) {

         auto head_block_num = ctrl.head_block_num();
         if (head_block_num <= 1) return vector<pbft_prepare>{};
         auto my_prepare = ctrl.get_pbft_my_prepare();

         auto reserve_prepare = [&](const block_id_type &in) {
            if (in == block_id_type{} || !ctrl.fetch_block_state_by_id(in)) return false;
            auto lib = ctrl.last_irreversible_block_id();
            if (lib == block_id_type{}) return true;
            auto forks = ctrl.fork_db().fetch_branch_from(in, lib);
            return !forks.first.empty() && forks.second.empty();
         };

         vector<pbft_prepare> new_pv;
         if (!prepares.empty()) {
            for (auto p : prepares) {
               //change uuid, sign again, update cache, then emit
               auto uuid = boost::uuids::to_string(uuid_generator());
               p.uuid = uuid;
               p.timestamp = time_point::now();
               p.producer_signature = ctrl.my_signature_providers()[p.public_key](p.digest());
               emit(pbft_outgoing_prepare, p);
            }
            return vector<pbft_prepare>{};
         } else if (reserve_prepare(my_prepare)) {
            for (auto const &sp : ctrl.my_signature_providers()) {
               auto uuid = boost::uuids::to_string(uuid_generator());
               auto my_prepare_num = ctrl.fetch_block_state_by_id(my_prepare)->block_num;
               auto p = pbft_prepare{uuid, current_view, my_prepare_num, my_prepare, sp.first, chain_id()};
               p.producer_signature = sp.second(p.digest());
               emit(pbft_outgoing_prepare, p);
               new_pv.emplace_back(p);
            }
            return new_pv;
         } else {

            auto current_watermark = get_current_pbft_watermark();
            auto lib = ctrl.last_irreversible_block_num();

            uint32_t high_watermark_block_num = head_block_num;

            if ( current_watermark > 0 ) {
               high_watermark_block_num = std::min(head_block_num, current_watermark);
            }

            if (high_watermark_block_num <= lib) return vector<pbft_prepare>{};

            if (auto hwbs = ctrl.fork_db().get_block_in_current_chain_by_num(high_watermark_block_num)) {

               for (auto const &sp : ctrl.my_signature_providers()) {
                  auto uuid = boost::uuids::to_string(uuid_generator());
                  auto p = pbft_prepare{uuid, current_view, high_watermark_block_num, hwbs->id, sp.first,
                                        chain_id()};
                  p.producer_signature = sp.second(p.digest());
                  add_pbft_prepare(p);
                  emit(pbft_outgoing_prepare, p);
                  new_pv.emplace_back(p);
                  ctrl.set_pbft_my_prepare(hwbs->id);
               }
            }
            return new_pv;
         }
      }



































   }}