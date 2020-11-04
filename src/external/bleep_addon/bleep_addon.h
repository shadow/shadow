#ifndef BLEEP_ADDON_H
#define BLEEP_ADDON_H


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
