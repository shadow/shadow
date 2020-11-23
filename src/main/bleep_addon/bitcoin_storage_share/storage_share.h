#ifndef BLEEP_BITCOIN_STORAGE_SHARE_H
#define BLEEP_BITCOIN_STORAGE_SHARE_H


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
