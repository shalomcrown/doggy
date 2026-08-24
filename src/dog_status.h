#ifndef DOG_STATUS_H
#define DOG_STATUS_H

#include <string>
#include <vector>

// ================================================================================

enum class DogErrorCode {
    i2c
};

// ================================================================================

class DogError {
public:
    DogErrorCode code;
    std::string message;
};

// ================================================================================

class DogStatus {
public:
    std::vector<DogError> errors;
};

#endif
