#include "log_map.h"

#include <iostream>
#include <map>
#include <string>
#include <string.h>


std::map<std::string, Node> block_map;

void update_log_map(const char prevblockhash[], const char blockhash[], const int txcount, const int height){
    int result = insertblock(prevblockhash,blockhash,txcount,height);
    if(!result ){
        printf("error!\n");
    }

}


int getPrevBlockTxcount(const char* prevblockhash) {
    if(memcmp(prevblockhash,"0000000000000000000000000000000000000000000000000000000000000000",sizeof(char)*32)==0) {
        return 0;
    }
    int prevblocktxcount = block_map.find(prevblockhash)->second.totalTxCnt;
    return prevblocktxcount;

}

Node newblock(const char* prevblockhash, const char* blockhash, int txcount, int height){
    Node node;
    node.height = height;
    node.txCnt = txcount;
    node.totalTxCnt = getPrevBlockTxcount(prevblockhash)+txcount;
    node.prevblockhash=(char*)malloc(sizeof(char)*32);
    node.prevblockhash=prevblockhash;
    node.blockhash=(char*)malloc(sizeof(char)*32);
    node.blockhash=blockhash;

    if(node.totalTxCnt>max_tx_cnt){
        max_tx_cnt = node.totalTxCnt;
    }

    return node;
}

int insertblock(const char* prevblockhash, const char* blockhash, int txcount,int height) {
    std::map<std::string, Node>::iterator it;
    Node node;
    node= newblock(prevblockhash,blockhash,txcount,height);
    block_map[blockhash]=node;
    it=block_map.find(blockhash);
    return 1;
}

//transaction total count
int get_tx_total_count() {
    return max_tx_cnt;
}

//transaction total count until this block
int get_tx_count(const char* blockhash){
    std::map<std::string, Node>::iterator it;
    it=block_map.find(blockhash);
    return it->second.totalTxCnt;
}
