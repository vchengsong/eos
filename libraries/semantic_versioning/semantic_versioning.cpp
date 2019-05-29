/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */

#pragma once

#include <eosio/semantic_versioning/semantic_versioning.hpp>

namespace eosio { namespace chain {
   version::version()
   :
    executable    {"${CLI_CLIENT_EXECUTABLE}"}
   ,version_major {"${VERSION_MAJOR}"        }
   ,version_minor {"${VERSION_MINOR}"        }
   ,version_patch {"${VERSION_PATCH}"        }
   ,version_suffix{"${VERSION_SUFFIX}"       }
   {
   }

   std::string version::version_client() {
      const char*                              command{"git describe --tags --dirty"};
      std::unique_ptr<FILE, decltype(&pclose)> pipe   {popen(cmd, "r"), pclose      };
      std::array<char, 64>                     buffer {};
      std::string                              result {};
      
      if (!pipe) {
         throw std::runtime_error{"popen() failed"};
      }
      while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
         result += buffer.data();
      }
      return result;
   }
      
   std::string version::version_full() {
      return std::string{"version_full"};
   }

   void version::check_if_dirty() {
      
   }
} } // namespace eosio::chain
