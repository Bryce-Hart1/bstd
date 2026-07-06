#include <fstream> //for file I/O in vault
#include <mutex> //for lock around file
#include <string>
#include "writeBin.hpp"
#include <vector>
namespace bstd{
namespace store{
/**
 * July 2nd 26
 * A even more lightweight file storage, basically SQL-lite.
 * multithreaded.
 * Made for learning purposes mainly. Just use SQL-lite if you need something serious.
 * dependencies -
 * fstream (for file IO)
 * Mutex (for std::mutex)
 * Thread (to assign thread work)
 * writeBin (for writing binary to file)
 */
class vault{
using usize = std::size_t;
using u16 = u_int16_t;
using u32 = u_int32_t;

  private:
    std::string _name;
    std::fstream _file;
    usize _pageSize;
    std::mutex _lock;
    u16 _magic;
    usize _pageCount;
    u16 _tableAmount;
    

    void updatePageCount(){
      this->_pageCount++; //increment page amount

    }

    static constexpr u16 MAGIC = 3214;

    // writes the header fields to offset 0, in a fixed order
    void writeHeaderInit(){
      _file.seekp(0);
      bstd::bit::writeBinary(_magic, _file);
      bstd::bit::writeBinary(_pageSize, _file);
      bstd::bit::writeBinary(_pageCount, _file);
      bstd::bit::writeBinary(_tableAmount, _file);
    }

    // reads the header fields back from offset 0, same order as writeHeader
    void readHeader(){
      _file.seekg(0);
      _magic = bstd::bit::readBinary<u16>(_file);
      if(_magic != MAGIC){
        throw std::runtime_error("vault: bad magic number, not a vault file: " + _name);
      }
      _pageSize = bstd::bit::readBinary<usize>(_file);
      _pageCount = bstd::bit::readBinary<usize>(_file);
      _tableAmount = bstd::bit::readBinary<u16>(_file);
    }
    private:
    //points at where the next write should occur 
    usize point_at() const{
      
    }


    public:

    /**
     * Define a new vault
     * @param pagesize - size of page in file, in bytes
     */
    vault(const usize pagesize, std::string name): _name(std::move(name)), _file(_name, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc), _pageSize(pagesize){
      _magic = MAGIC;
      _pageCount = 0;
      _tableAmount = 0;
      writeHeaderInit();
    }

    public:
    /**
     * Open an existing vault file and load its header.
     */
    explicit vault(std::string name): _name(std::move(name)), 
    _file(_name, std::ios::in | std::ios::out | std::ios::binary), 
    _pageSize(0){
      if(!_file)
        throw std::runtime_error("vault: could not open file: " + _name);
    }

    public:
    bool doesTableExist(const std::string_view &table) {
      try{
        readHeader(); //update header pos
      }catch(const std::exception& e){
        std::cerr << e.what() << std::endl;
      }

    }


    public:
    void write(const std::string &index, const std::string_view &message){
      if(doesTableExist(index)){
        //allocate size 
        usize messageInBytes = sizeof(message);


      }
      throw std::runtime_error("table requested: " + index + " does not exist");
    }

    //if process running fails/exits, reclaims written files in memory
    void reclaim(){

    }

};







}//namespace storage
}//namespace bstd