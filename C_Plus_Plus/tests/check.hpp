#pragma once
// Tiny test harness shared by the *Test.cpp files. Each test is its own program: run its checks, then
// `return finishTests();` from main. tests/run.sh builds and runs all of them.
#include <iostream>

inline int failures = 0;
inline int checks = 0;

#define CHECK(cond)                                                                    \
    do{                                                                                \
        checks++;                                                                      \
        if(!(cond)){                                                                   \
            failures++;                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed\n"; \
        }                                                                              \
    } while(0)

/* Both sides are copied (so nothing can dangle) and need operator<< for the failure message. */
#define CHECK_EQ(a, b)                                                                       \
    do{                                                                                      \
        checks++;                                                                            \
        const auto checkA = (a);                                                            \
        const auto checkB = (b);                                                            \
        if(!(checkA == checkB)){                                                             \
            failures++;                                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " failed\n  left:  " \
                      << checkA << "\n  right: " << checkB << "\n";                          \
        }                                                                                    \
    } while(0)

#define CHECK_THROWS(expr, Exception)                                                         \
    do{                                                                                       \
        checks++;                                                                             \
        bool threw = false;                                                                   \
        try{ (void)(expr); }                                                                  \
        catch(const Exception&){ threw = true; }                                              \
        if(!threw){                                                                           \
            failures++;                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #expr " did not throw " #Exception "\n"; \
        }                                                                                     \
    } while(0)

inline int finishTests(){
    if(failures){
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "all " << checks << " checks passed\n";
    return 0;
}
