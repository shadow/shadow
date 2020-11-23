#ifndef BLEEP_BITCOIN_BLOCK_STORAGE_ENTRY_H
#define BLEEP_BITCOIN_BLOCK_STORAGE_ENTRY_H


#ifdef __cplusplus
extern "C"
{
#endif
void shadow_bitcoin_register_hash(const char hash[]);
int shadow_bitcoin_check_hash(const char hash[]);
#ifdef __cplusplus
}
#endif


#endif
