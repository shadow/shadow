#include "storage_share.h"

#include <vector>
#include <unordered_map>

class blockdat_hash_entry {

};

std::vector<blockdat_hash_entry> blockdat_tbl_head;

void AddHashData(int fileno, char* actual_path, char* lastBlockHash){

    // list entry 생성
    Hashlist* elem = (Hashlist*)malloc(sizeof(Hashlist));
    elem->fileno = fileno;
    elem->actual_path = actual_path;
    elem->lastBlockHashMerkleRoot = (char*)malloc(sizeof(char)*32);
    memcpy(elem->lastBlockHashMerkleRoot, lastBlockHash, 32);
    elem->refCnt=0;

    // put elem to list header
    Hashlist* cursor = hashtable->ents[fileno].list;
    hashtable->ents[fileno].list = elem;
    elem->next = cursor;
    elem->prev = NULL;
    if (cursor)
        cursor->prev = elem;

    hashtable->ents[fileno].listcnt++;
}




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