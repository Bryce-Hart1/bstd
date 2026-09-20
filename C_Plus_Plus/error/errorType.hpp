#pragma once
#include <string>


class error_t{
    private:
    std::string _message;
    bool _OK;
    
    public:
    
    error_t(const bool is_ok, const std::string message){
        _message = message;
    }

    error_t ok(){
        return {true, "none"};
    }

    error_t notOk(){
        return {false, _message};
    }

    error_t notOkNewMessage(const std::string& new_message){
        return {false, new_message};
    }
    

    const std::string_view whatIsError() const noexcept{
        if(_OK){
            return {};
        }else{
            return {_message};
        }
    }

};