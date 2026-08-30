#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include "dog_api.h"

// ================================================================================

class SystemControl {
public:
    virtual ~SystemControl() = default;
    virtual void perform(SystemAction action) = 0;
};

// ================================================================================

class NullSystemControl : public SystemControl {
public:
    void perform(SystemAction action) override;
};

// ================================================================================

class SystemdControl : public SystemControl {
public:
    void perform(SystemAction action) override;
};

#endif
