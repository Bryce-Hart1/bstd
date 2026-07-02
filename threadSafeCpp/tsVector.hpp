#pragma once

#include <shared_mutex>
#include<mutex>
#include <thread>
#include <iostream>
#include <cstddef>
#include "exceptions.hpp"

/** Bryce Hart
 * @attention complete vector implementation
 * My goal with this project is to provide a completely safe multithreaded vector 
 * implementation with all functions mostly from scratch
 *
 * 
 */

namespace bstd{
 namespace ds{
namespace ts{

    /** @class vec
    * threadsafe vector implementation
    * 
    */
    template <typename T> class vec{
        using sizeT = std::size_t;

        private:
        T* v_Data;
        sizeT v_Size; //also our end iterator
        sizeT v_Capacity;
        sizeT v_StartIndex;
        mutable std::shared_mutex v_mutex;
        

        /**
         * @attention if adding private members, do NOT add locks. Public member is already locking.
         * 
         */

        //checks if index is in range
        bool p_checkIndex(sizeT index) const {
            if(index >= v_StartIndex && index <= v_Size){
                return true;
            }
            return false;
        }

        //overload checking if both indexes are in range
        bool p_checkIndex(sizeT index1, sizeT index2) const {
            if(v_StartIndex <= index1 && index1 <= v_Size && v_StartIndex <= index2 && index2 <= v_Size){
                return true;
            }
            return false;
        }

        T* p_returnCopy(sizeT newSize){
            T* newBlock = new T[newSize]; 
                for (sizeT i = 0; i < v_Size; i++){
                    newBlock[i] = v_Data[i];
                }
                delete[] v_Data; 
                return newBlock;
        }

        /**
         * @brief private member that moves ontop of index provided. 
         * ex: [1] [2] [3]
         * provide 1 (index 1)
         * : [1] [3] [3]
         * must condense size by one to compensate for this
         * size = 2
         * [1] [3]
         */
        void p_moveLeft(sizeT moveOnto){
            for(int i = moveOnto; i < v_Size-1; i++){
                v_Data[i] = v_Data[i+1];
            }
        }


        /**
         * @brief overload helper for remove, removes all elements from onto to the end index of the 
         * gap. This overload helps us do one loop instead of several
         */
        void p_removeOverloadHelper(sizeT start, sizeT end){
            sizeT grabFrom = end+1;
            while(grabFrom <= v_Size){
                v_Data[start] = v_Data[grabFrom];
                start++;
                grabFrom++;
            }
        }

        //must already be resized to +1, and the startFrom index will exist twice, once at i = 0, and i = 1
        void p_moveRight(std::size_t startFrom){
            for(int i = 0; i < v_Size; i++){
                v_Data[i+1] = v_Data[i];
            }
        }


        public: 
        vec() : v_Data(new T[2]), v_Size(0), v_Capacity(2), v_StartIndex(0){}

        /*passing intial size allows for vec size to be set to this*/
        vec(sizeT v_Capacity) : v_Data(new T[v_Capacity]), v_Size(0), v_Capacity(2), v_StartIndex(0){}

        ~vec(){ delete[] v_Data; }

        /**
         * @brief append a value to the desired index and moves right the index in that position
         * 
         */
        void appendTo(sizeT indexToAppend, T value){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            try{
                if(p_checkIndex(indexToAppend)){
                    if(v_Size == v_Capacity){
                        v_Data = p_returnCopy(v_Capacity * 2);
                        v_Capacity *= 2;
                    }
                    p_moveRight(indexToAppend);
                    v_Data[indexToAppend] = value;
                    return;
                }
                throw std::out_of_range("index out of range");
            }catch(std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }

        T at(const sizeT indexAt) const{
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            try{
                if(p_checkIndex(indexAt)){
                    return v_Data[indexAt];
                }
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
            
        }

        //return current possible capacity of vec
        sizeT capacity(){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            return this->v_Capacity;
        }

        //clears all elements of vector but does not shrink size
        void clear(){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            v_Data = new T[v_Capacity];
            v_Size = v_StartIndex = 0;

        }

        //I assume this would not be the defaulted wanted behavior, but it is an option that I will provide

        //clears all elements and resets size
        void clear(const bool RESET_CAPACITY){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            v_Capacity = 2;
            clear();

        }


        //retrieve last element in vector
        T endElement(){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            try{
                if(size() != 0){
                    return v_Data[v_Size];
                }else{
                    throw std::out_of_range("index out of range");
                }
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }


        sizeT endItr(){
            return v_Size; //pull back at current moment
        }

        /** @brief a better named erase() from std::vector
         * moves down elements at that position
         * 
         */
        void eraseAt(const std::size_t iteratorPosition){
            try{
                if(p_checkIndex(iteratorPosition)){

                }
                throw std::out_of_range("index out of range");
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }

        /**
         * returns front element
         */
        T frontElement() const{
            try{
                if(this->v_Size != 0){
                    return v_Data[v_StartIndex];
                }else{
                    throw std::out_of_range("index out of range");
                }
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }


        bool isEmpty() const {
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            if(v_Size == 0){
                return true;
            }
            return false;
        }
        
        //remove last element
        void popBack(){
            std::unique_lock<std::shared_mutex> lock(v_mutex);
            v_Size--;
        }

        
        /**
         * @details pushBack
         * Push value of vec type or push back a vec of same type
         * @param value - value or values to pushback
         */
        void pushBack(T value){
            std::unique_lock<std::shared_mutex> lock(v_mutex);
            if(v_Size == v_Capacity){
                size_t allocateNewSize = (v_Capacity * 2);
                v_Data = p_returnCopy(allocateNewSize);   
                v_Capacity = allocateNewSize;                
            }
            v_Data[v_Size] = value;
            v_Size++;
        }


        //removes from the index provided
        void remove(sizeT IndexToRemove){
        std::shared_lock<std::shared_mutex> lock(v_mutex);
            p_moveLeft(IndexToRemove);
        }
        //removes from all indexes from starting index to ending index 
        void remove(sizeT startIndex, sizeT endIndex){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            try{
                if(p_checkIndex(startIndex, endIndex)){
                    p_removeOverloadHelper(startIndex, endIndex);
                    v_Size -= (endIndex - startIndex);
                }
                throw std::out_of_range("Index is out of range in Remove(start, end)");
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }

        /**
         * @brief overwrites element at desired index
         */
        void replace(T value, sizeT index){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            try{
                if(p_checkIndex(index)){
                    v_Data[index] = value;
                }
                throw std::out_of_range("Index is out of range");
            }catch(const std::exception &e){
                std::cerr << e.what() << '\n';
            }
        }

        //Capacity of vec becomes resizeToSize
        void resize(sizeT resizeToSize){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            v_Capacity = resizeToSize;
        }

        /**
         * @brief simular to shrink to fit for vector. Changes so the array takes up exactly the size in memory
         */
        void shrinkToFit(){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            this->v_Capacity = this->v_Size;
        }

        //returns size of this vec object
        sizeT size() const {
            std::shared_lock lock(v_mutex);
            return this->v_Size;
        }


        // returns starting iterator position
        sizeT startItr(){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            return this->v_StartIndex;
        }

        
        void swap(T &dataA, T &dataB){
            std::shared_lock<std::shared_mutex> lock(v_mutex);
            T temp = dataA;
            dataA = dataB;
            dataB = temp;
        }

    };
}//ts

 }//ds
}//bstd