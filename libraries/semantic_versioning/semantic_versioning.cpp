/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */

#pragma once

#include <eosio/semantic_versioning/semantic_versioning.hpp>

namespace eosio { namespace chain {
   version::version(const std::string exe, const std::string& vmaj, std::string& vmin, std::string& vp, std::string& vrc, std::string& vs)
   : executable{exe}, version_major{vmaj}, version_minor{vmin}, version_patch{vp}, version_rc{vrc}, version_status{vs}
   {
   }

   std::string version::version_client() {
      
   }
      
   std::string version::version_full() {
      
   }

   void version::check_if_dirty() {
      
   }
} } // namespace eosio::chain
