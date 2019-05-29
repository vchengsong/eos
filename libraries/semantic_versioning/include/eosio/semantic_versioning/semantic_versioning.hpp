/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */

#pragma once

#include <string>

// if !defined("${VERSION_MAJOR}") || !defined("${VERSION_MINOR}") || !defined("${VERSION_PATCH}")
// #error 
// #endif

namespace eosio { namespace chain {
   class version {
   public:
      version(const std::string exe, const std::string& vmaj, std::string& vmin, std::string& vp, std::string& vrc, std::string& vs);

      std::string version_client(); ///<
      std::string version_full();   ///<

   private:
      const std::string executable;     ///< 
      const std::string version_major;  ///< 
      const std::string version_minor;  ///< 
      const std::string version_patch;  ///< 
      const std::string version_rc;     ///< 
      const std::string version_status; ///<

      void check_if_dirty(); ///<
   };
} } // namespace eosio::chain
