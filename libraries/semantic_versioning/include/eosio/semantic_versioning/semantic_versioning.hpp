/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */

#pragma once

#include <cstdlib> // system

#include <array>   // std::array
#include <string>  // std::string

namespace eosio { namespace chain {
   class version {
   public:
      version();

      static std::string version_client();   ///<
      static std::string version_full();     ///<
      void               validate_version(); ///<

   private:
      const std::string executable;     ///< 
      const std::string version_major;  ///< 
      const std::string version_minor;  ///< 
      const std::string version_patch;  ///< 
      const std::string version_suffix; ///< 
      const std::string version_status; ///< 

      void check_if_dirty(); ///<
   };
} } // namespace eosio::chain
