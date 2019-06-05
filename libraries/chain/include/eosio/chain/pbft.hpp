/**
 *  @file
 *  @copyright defined in bos/LICENSE
 */
#pragma once

#include <eosio/chain/controller.hpp>

namespace eosio { namespace chain {
   using std::vector;

   struct psm_cache {
      vector<pbft_prepare>                         prepares;
      vector<pbft_commit>                          commits;
      vector<pbft_view_change>                     view_changes;
      vector<pbft_prepared_certificate>            prepared_certificates;
      vector<vector<pbft_committed_certificate>>   committed_certificates;
      vector<pbft_view_changed_certificate>        view_changed_certificates;
   };

   class psm_state;
   class psm_prepared_state;
   class psm_committed_state;
   class psm_view_change_state;

   class psm_machine {
   public:
      explicit psm_machine( pbft_database& pbft_db );
      ~psm_machine();

      void set_current( psm_state *s ) { current = s; }

      psm_state* get_current() { return current; }

      void on_prepare( pbft_prepare& prepare );
      void on_commit( pbft_commit& commit );
      void on_view_change( pbft_view_change& view_change );
      void on_new_view( pbft_new_view& new_view );

      void send_prepare();
      void send_commit();
      void send_view_change();

      template<typename T>
      void transit_to_committed_state( T const & from_state, bool to_new_view );

      template<typename T>
      void transit_to_prepared_state( T const & from_state );

      template<typename T>
      void transit_to_view_change_state( T const & from_state );

      template<typename T>
      void transit_to_new_view( T const & from_state, const pbft_new_view& new_view );

      void send_pbft_view_change();

      const vector<pbft_prepare>&                        get_prepares_cache() const;
      const vector<pbft_commit>&                         get_commits_cache() const;
      const vector<pbft_view_change>&                    get_view_changes_cache() const;
      const vector<pbft_prepared_certificate>&           get_prepared_certificate() const;
      const vector<vector<pbft_committed_certificate>>&  get_committed_certificate() const;
      const vector<pbft_view_changed_certificate>&       get_view_changed_certificate() const;

      const uint32_t&   get_target_view() const;
      const uint32_t&   get_target_view_retries() const;
      const uint32_t&   get_view_change_timer() const;
      const uint32_t&   get_current_view() const;

      void set_prepares_cache( const vector<pbft_prepare> &pcache );
      void set_commits_cache( const vector<pbft_commit> &ccache );
      void set_view_changes_cache( const vector<pbft_view_change> &vc_cache );
      void set_current_view( const uint32_t &cv );
      void set_prepared_certificate( const vector<pbft_prepared_certificate> &pcert );
      void set_committed_certificate( const vector<vector<pbft_committed_certificate>> &ccert );
      void set_view_changed_certificate( const vector<pbft_view_changed_certificate> &vc_cert );

      void set_target_view_retries( const uint32_t &tv_reties );
      void set_target_view( const uint32_t &tv );
      void set_view_change_timer( const uint32_t &vc_timer );
      void manually_set_current_view( const uint32_t &cv );

   public:
      pbft_database& pbft_db;

   private:
      psm_state*              current;
      psm_prepared_state      prepared;
      psm_committed_state     committed;
      psm_view_change_state   view_change;

   private:
      psm_cache   cache;
      uint32_t    current_view;
      uint32_t    target_view;
      uint32_t    target_view_retries;
      uint32_t    view_change_timer;
   };


   class psm_state {
   public:
      psm_state();
      ~psm_state();

      virtual void on_prepare( psm_machine& sm, pbft_prepare& prepare ) = 0;
      virtual void on_commit( psm_machine& sm, pbft_commit& commit ) = 0;
      virtual void on_view_change( psm_machine& sm, pbft_view_change& view_change ) = 0;
      virtual void on_new_view( psm_machine& sm, pbft_new_view& new_view ) = 0;

      virtual void send_prepare( psm_machine& sm ) = 0;
      virtual void send_commit( psm_machine& sm ) = 0;
      virtual void send_view_change( psm_machine& sm ) = 0;

      virtual const char* get_name() = 0;
   };

   class psm_prepared_state final: public psm_state {
   public:
      psm_prepared_state();
      ~psm_prepared_state();

      void on_prepare( psm_machine& sm, pbft_prepare& prepare ) override;
      void on_commit( psm_machine& sm, pbft_commit& commit ) override;
      void on_view_change( psm_machine& sm, pbft_view_change& view_change ) override;
      void on_new_view( psm_machine& sm, pbft_new_view& new_view ) override;

      void send_prepare( psm_machine& sm ) override;
      void send_commit( psm_machine& sm ) override;
      void send_view_change( psm_machine& sm ) override;

      bool pending_commit_local;
      const char* get_name() override { return "prepared"; }
   };

   class psm_committed_state final: public psm_state {
   public:
      psm_committed_state();
      ~psm_committed_state();

      void on_prepare( psm_machine& sm, pbft_prepare& prepare ) override;
      void on_commit( psm_machine& sm, pbft_commit& commit ) override;
      void on_view_change( psm_machine& sm, pbft_view_change& view_change ) override;
      void on_new_view( psm_machine& sm, pbft_new_view& new_view ) override;

      void send_prepare( psm_machine& sm ) override;
      void send_commit( psm_machine& sm ) override;
      void send_view_change( psm_machine& sm ) override;

      const char* get_name() override { return "committed"; }
   };

   class psm_view_change_state final: public psm_state {
   public:
      void on_prepare( psm_machine& sm, pbft_prepare& prepare ) override;
      void on_commit( psm_machine& sm, pbft_commit& commit ) override;
      void on_view_change( psm_machine& sm, pbft_view_change& view_change ) override;
      void on_new_view( psm_machine& sm, pbft_new_view& new_view ) override;

      void send_prepare( psm_machine& sm ) override;
      void send_commit( psm_machine& sm ) override;
      void send_view_change( psm_machine& sm ) override;

      const char* get_name() override { return "view change"; }
   };

   struct pbft_config {
      uint32_t view_change_timeout;
      bool     bp_candidate;
   };

   class pbft_controller {
   public:
      pbft_controller( controller& ctrl );
      ~pbft_controller();

      pbft_database  pbft_db;
      psm_machine    state_machine;
      pbft_config    config;

      void maybe_pbft_prepare();
      void maybe_pbft_commit();
      void maybe_pbft_view_change();
      void send_pbft_checkpoint();

      void on_pbft_prepare( pbft_prepare& prepare );
      void on_pbft_commit( pbft_commit& commit );
      void on_pbft_view_change( pbft_view_change& view_change );
      void on_pbft_new_view( pbft_new_view& new_view );
      void on_pbft_checkpoint( pbft_checkpoint& checkpoint );

   private:
      fc::path data_dir;
   };



}}




