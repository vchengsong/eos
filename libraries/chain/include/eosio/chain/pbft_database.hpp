/**
 *  @file
 *  @copyright defined in bos/LICENSE
 */
#pragma once

#include <eosio/chain/controller.hpp>

namespace eosio { namespace chain {

   enum pbft_msg_type : uint8_t {
      pbft_prepare,
      pbft_commit,
   };

   struct pbft_prepare {
      pbft_msg_type     data_type = pbft_prepare;
      uint32_t          view;
      block_id_type     block_id;
      time_point        timestamp;
      public_key_type   public_key;
      signature_type    producer_signature;
   }

   struct pbft_commit {
      pbft_msg_type     data_type = pbft_commit;
      uint32_t          view;
      block_id_type     block_id;
      time_point        timestamp;
      public_key_type   public_key;
      signature_type    producer_signature;
   }

   struct pbft_checkpoint {
      block_id_type     block_id;
      time_point        timestamp;
      public_key_type   public_key;
      signature_type    producer_signature;
   }

   struct pbft_stable_checkpoint {
      block_id_type           block_id;
      vector<pbft_checkpoint> checkpoints;
   }

   struct pbft_prepared_certificate {
      block_id_type           block_id;
      vector<pbft_prepare>    prepares;
   }

   struct pbft_committed_certificate {
      block_id_type           block_id;
      vector<pbft_commit>     commits;
   }

   struct pbft_view_change {
      uint32_t                            current_view;
      pbft_prepared_certificate           prepared;
      vector<pbft_committed_certificate>  committeds;
      pbft_stable_checkpoint              stable_checkpoint;
      time_point                          timestamp;
      public_key_type                     public_key;
      signature_type                      producer_signature;
   }

   struct pbft_view_changed_certificate {
      uint32_t                      view;
      vector<pbft_view_change>            view_changes;
   }

   struct pbft_new_view {
      uint32_t                      view;
      pbft_prepared_certificate           prepared;
      vector<pbft_committed_certificate>  committeds;
      pbft_stable_checkpoint              stable_checkpoint;
      pbft_view_changed_certificate       view_changed;
      time_point                          timestamp;
      public_key_type                     public_key;
      signature_type                      producer_signature;
   }

   struct pbft_ppcm_state {
      block_id_type        block_id;
      uint32_t             block_num;
      vector<pbft_prepare> prepares;
      bool                 should_prepared = false;
      vector<pbft_commit>  commits;
      bool                 should_committed = false;
   };

   struct pbft_view_state {
      uint32_t                   view;
      vector<pbft_view_change>   view_changes;
      bool                       should_view_changed = false;
   };

   struct pbft_checkpoint_state {
      block_id_type              block_id;
      uint32_t                   block_num;
      vector<pbft_checkpoint>    checkpoints;
      bool                       is_stable = false;
   };

   using pbft_ppcm_state_ptr        = std::shared_ptr<pbft_ppcm_state>;
   using pbft_view_state_ptr        = std::shared_ptr<pbft_view_state>;
   using pbft_checkpoint_state_ptr  = std::shared_ptr<pbft_checkpoint_state>;

   struct by_block_id;
   struct by_block_num;
   struct by_should_prepared_and_block_num;
   struct by_should_commited_and_block_num;
   typedef multi_index_container<
      pbft_ppcm_state_ptr,
      indexed_by<
         hashed_unique<
            tag<by_block_id>,
            member<pbft_ppcm_state, block_id_type, &pbft_ppcm_state::block_id>,
            std::hash<block_id_type>
         >,
         ordered_non_unique<
            tag<by_block_num>,
            member<pbft_ppcm_state, uint32_t, &pbft_ppcm_state::block_num>
         >,
         ordered_non_unique<
            tag<by_should_prepared_and_block_num>,
            composite_key<
               pbft_ppcm_state,
               member<pbft_ppcm_state, bool, &pbft_ppcm_state::should_prepared>,
               member<pbft_ppcm_state, uint32_t, &pbft_ppcm_state::block_num>
            >,
            composite_key_compare<greater<>, greater<>>
         >,
         ordered_non_unique<
            tag<by_should_commited_and_block_num`>,
            composite_key<
               pbft_ppcm_state,
               member<pbft_ppcm_state, bool, &pbft_ppcm_state::should_committed>,
               member<pbft_ppcm_state, uint32_t, &pbft_ppcm_state::block_num>
            >,
            composite_key_compare<greater<>, greater<>>
         >
      >
   >
   pbft_ppcm_state_multi_index_type;

   struct by_view;
   struct by_should_view_changed_and_view;
   typedef multi_index_container<
      pbft_view_state_ptr,
      indexed_by<
         ordered_unique<
            tag<by_view>,
            member<pbft_view_state, uint32_t, &pbft_view_state::view>
         >,
         ordered_non_unique<
            tag<by_should_view_changed_and_view>,
            composite_key<
               pbft_view_state,
               member<pbft_view_state, bool, &pbft_view_state::should_view_changed>,
               member<pbft_view_state, uint32_t, &pbft_view_state::view>
            >,
            composite_key_compare<greater<>, greater<>>
         >
      >
   >
   pbft_view_state_multi_index_type;

   struct by_block_id;
   struct by_num;
   typedef multi_index_container<
      pbft_checkpoint_state_ptr,
      indexed_by<
         hashed_unique<
            tag<by_block_id>,
            member<pbft_checkpoint_state, block_id_type, &pbft_checkpoint_state::block_id>,
            std::hash<block_id_type>
         >,
         ordered_non_unique<
            tag<by_num>,
            member<pbft_checkpoint_state, uint32_t, &pbft_checkpoint_state::block_num>
         >
      >
   >
   pbft_checkpoint_state_multi_index_type;


   class pbft_database {
   public:
      explicit pbft_database( controller& c );
      ~pbft_database();
      void close();

      bool     should_prepared();
      bool     should_committed();
      uint32_t should_view_change();
      bool     should_new_view(uint32_t target_view);

      bool     is_new_primary(uint32_t target_view);

      uint32_t get_proposed_new_view_num();

      void add_pbft_prepare( pbft_prepare& prepare );
      void add_pbft_commit( pbft_commit& commit );
      void add_pbft_view_change( pbft_view_change& view_change );
      void add_pbft_checkpoint( pbft_checkpoint& checkpoint );

      vector<pbft_prepare> send_and_add_pbft_prepare( 
         const vector<pbft_prepare>& prepares = vector<pbft_prepare>{}, 
         uint32_t current_view = 0 );

      vector<pbft_commit> send_and_add_pbft_commit( 
         const vector<pbft_commit>& commits = vector<pbft_commit>{}, 
         uint32_t current_view = 0 );

      vector<pbft_view_change> send_and_add_pbft_view_change(
         const vector<pbft_view_change>& view_changes = vector<pbft_view_change>{},
         const vector<pbft_prepared_certificate>& prepared_certificates = vector<pbft_prepared_certificate>{},
         const vector<vector<pbft_committed_certificate>>& committed_certificate_vv = vector<vector<pbft_committed_certificate>>{},
         uint32_t current_view = 0,
         uint32_t new_view = 1 );

      pbft_new_view send_pbft_new_view(
         const vector<pbft_view_changed_certificate>& view_changed_certificates = vector<pbft_view_changed_certificate>{},
         uint32_t current_view = 1 );

      vector<pbft_checkpoint> generate_and_add_pbft_checkpoint();

      bool is_valid_prepare(const pbft_prepare &p);

      bool is_valid_commit(const pbft_commit &c);

      void commit_local();

      bool pending_pbft_lib();

      void prune_pbft_index();

      uint32_t get_committed_view();

      chain_id_type chain_id();

      vector<pbft_prepared_certificate> generate_prepared_certificate();

      vector<vector<pbft_committed_certificate>> generate_committed_certificate();

      vector<pbft_view_changed_certificate> generate_view_changed_certificate(uint32_t target_view);

      pbft_stable_checkpoint get_stable_checkpoint_by_id(const block_id_type &block_id);

      pbft_stable_checkpoint fetch_stable_checkpoint_from_blk_extn(const signed_block_ptr &b);

      block_info cal_pending_stable_checkpoint() const;

      bool should_send_pbft_msg();

      bool should_recv_pbft_msg(const public_key_type &pub_key);

      public_key_type get_new_view_primary_key(uint32_t target_view);

      void send_pbft_checkpoint();

      void checkpoint_local();

      bool is_valid_checkpoint(const pbft_checkpoint &cp);

      bool is_valid_stable_checkpoint(const pbft_stable_checkpoint &scp);

      signal<void(const pbft_prepare &)> pbft_outgoing_prepare;
      signal<void(const pbft_prepare &)> pbft_incoming_prepare;

      signal<void(const pbft_commit &)> pbft_outgoing_commit;
      signal<void(const pbft_commit &)> pbft_incoming_commit;

      signal<void(const pbft_view_change &)> pbft_outgoing_view_change;
      signal<void(const pbft_view_change &)> pbft_incoming_view_change;

      signal<void(const pbft_new_view &)> pbft_outgoing_new_view;
      signal<void(const pbft_new_view &)> pbft_incoming_new_view;

      signal<void(const pbft_checkpoint &)> pbft_outgoing_checkpoint;
      signal<void(const pbft_checkpoint &)> pbft_incoming_checkpoint;

      bool is_valid_view_change(const pbft_view_change &vc);

      bool is_valid_new_view(const pbft_new_view &nv);

      bool should_stop_view_change(const pbft_view_change &vc);

      pbft_ppcm_state_ptr get_pbft_ppcm_state_by_id(const block_id_type& id)const;

      vector<pbft_checkpoint_state> get_checkpoints_by_num(const uint32_t& num)const;

      pbft_view_state_ptr get_view_changes_by_target_view(const uint32_t& tv)const;

      vector<uint32_t> get_pbft_watermarks()const;

      flat_map<public_key_type, uint32_t> get_pbft_fork_schedules()const;

      uint32_t get_current_pbft_watermark();

      void update_fork_schedules();

      
   private:
      controller&                                  ctrl;
      uint32_t                                     current_view;
      pbft_ppcm_state_multi_index_type             ppcm_state_index;
      pbft_checkpoint_state_multi_index_type       checkpoint_index;
      pbft_view_state_multi_index_type             view_state_index;
      vector<uint32_t>                             prepare_watermarks;
      flat_map<public_key_type, uint32_t>          fork_schedules;

   private:
      bool is_valid_prepared_certificate( const pbft_prepared_certificate &certificate );
      bool is_valid_committed_certificate( const pbft_committed_certificate &certificate );

      vector<vector<block_info>> fetch_fork_from(vector<block_info> block_infos);

      vector<block_info> fetch_first_fork_from(vector<block_info> &bi);

      bool is_valid_longest_fork(
         const block_info &bi,
         vector<block_info> block_infos,
         unsigned long threshold,
         unsigned long non_fork_bp_count);

      producer_schedule_type lscb_active_producers() const;

      template<typename Signal, typename Arg>
      void emit(const Signal &s, Arg &&a);

      void set(pbft_ppcm_state_ptr s);

      void set(pbft_checkpoint_state_ptr s);

      void prune(const pbft_ppcm_state_ptr &h);

      void prune_checkpoints(const pbft_checkpoint_state_ptr &h);

      
      
      
      
   };
   

}} // namespace eosio::chain

