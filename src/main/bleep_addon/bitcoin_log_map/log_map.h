#ifndef BLEEP_BITCOIN_LOG_MAP_H
#define BLEEP_BITCOIN_LOG_MAP_H

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Node{
        int height;
        int txCnt;
        int totalTxCnt;
        const char* prevblockhash;
        const char* blockhash;
}Node;

int max_tx_cnt=0;

Node newblock(const char* prevblockhash, const char* blockhash, int txcount, int height);
int getPrevBlockTxcount(const char* prevblockhash);
int insertblock(const char* prevblockhash, const char* blockhash, int txcount,int height);

void update_log_map(const char prevblockhash[], const char blockhash[], const int txcount, const int height);
int get_tx_total_count();
int get_tx_count(const char* blockhash);



#ifdef __cplusplus
}
#endif


#endif
