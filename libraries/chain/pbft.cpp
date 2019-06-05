/**
 *  @file
 *  @copyright defined in bos/LICENSE
 */
#include <eosio/chain/pbft.hpp>
#include <fc/io/fstream.hpp>
#include <fstream>


namespace eosio { namespace chain {

      pbft_controller::pbft_controller(controller& ctrl):pbft_db(ctrl), state_machine(pbft_db) {
         config.view_change_timeout = 6;
         config.bp_candidate = true;
      }

      pbft_controller::~pbft_controller() {}

      void pbft_controller::maybe_pbft_prepare() {
         if (!pbft_db.should_send_pbft_msg()) return;
         state_machine.send_prepare();
      }

      void pbft_controller::maybe_pbft_commit() {
         if (!pbft_db.should_send_pbft_msg()) return;
         state_machine.send_commit();
      }

      void pbft_controller::maybe_pbft_view_change() {
         if (!pbft_db.should_send_pbft_msg()) return;
         if (state_machine.get_view_change_timer() <= config.view_change_timeout) {
            if (!state_machine.get_view_changes_cache().empty()) {
               pbft_db.send_and_add_pbft_view_change(state_machine.get_view_changes_cache());
            }
            state_machine.set_view_change_timer(state_machine.get_view_change_timer() + 1);
         } else {
            state_machine.set_view_change_timer(0);
            state_machine.send_view_change();
         }
      }

      void pbft_controller::on_pbft_prepare(pbft_prepare &p) {
         if (!config.bp_candidate) return;
         state_machine.on_prepare(p);
      }

      void pbft_controller::on_pbft_commit(pbft_commit &c) {
         if (!config.bp_candidate) return;
         state_machine.on_commit(c);
      }

      void pbft_controller::on_pbft_view_change(pbft_view_change &vc) {
         if (!config.bp_candidate) return;
         state_machine.on_view_change(vc);
      }

      void pbft_controller::on_pbft_new_view(pbft_new_view &nv) {
         if (!config.bp_candidate) return;
         state_machine.on_new_view(nv);
      }

      void pbft_controller::send_pbft_checkpoint() {
         if (!pbft_db.should_send_pbft_msg()) return;
         pbft_db.send_pbft_checkpoint();
         pbft_db.checkpoint_local();
      }

      void pbft_controller::on_pbft_checkpoint(pbft_checkpoint &cp) {
         pbft_db.add_pbft_checkpoint(cp);
         pbft_db.checkpoint_local();
      }


      
      
      
      
      
      
      
      
      


}} 