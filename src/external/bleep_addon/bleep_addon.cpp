#include "bleep_addon.h"

#include <unordered_set>

/* bitcoin coinflip validation */
std::unordered_set<std::string> _bitcoin_coinflip_validation_table;

extern "C"
{

void shadow_bitcoin_register_hash(const char hash[]) {
    _bitcoin_coinflip_validation_table.insert(hash);
    return;
}
int shadow_bitcoin_check_hash(const char hash[]) {
    std::unordered_set<std::string>::const_iterator got = _bitcoin_coinflip_validation_table.find(hash);
    int res = (got != _bitcoin_coinflip_validation_table.end());
    return res;
}

}