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
      
      void pbft_database::add_pbft_prepare( pbft_prepare& prepare ){
         if( ! is_valid_prepare( prepare )) return;

         auto& by_block_id_index = ppcm_state_index.get<by_block_id>();
         auto current_bsp = ctrl.fetch_block_state_by_id( prepare.block_id );
         auto active_producers = current_bsp->active_producers.producers;

         // is in active bps

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

         auto &by_block_id_index = pbft_state_index.get<by_block_id>();
         auto current_bsp = ctrl.fetch_block_state_by_id( commit.block_id );
         auto active_producers = current_bsp->active_producers.producers;

         // is in active bps

         while( current_bsp && current_bsp->block_num > ctrl.last_irreversible_block_num() ){
            auto ppcm_state_itr = by_block_id_index.find( current_bsp->id );

            if ( ppcm_state_itr == by_block_id_index.end() ) {
               try {
                  auto state = pbft_ppcm_state{current_bsp->id, current_bsp->block_num, .commits={commit}};
                  auto state_ptr = make_shared<pbft_state>( state );
                  pbft_state_index.insert( state_ptr );
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


























































   }}