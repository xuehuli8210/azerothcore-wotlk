// LoginQueryHolder.h
#ifndef LOGINQUERYHOLDER_H
#define LOGINQUERYHOLDER_H

#include "DatabaseEnv.h"
#include "ObjectGuid.h"

class LoginQueryHolder : public CharacterDatabaseQueryHolder
{
private:
    uint32 m_accountId;
    ObjectGuid m_guid;
public:
    LoginQueryHolder(uint32 accountId, ObjectGuid guid)
        : m_accountId(accountId), m_guid(guid) { }

    ObjectGuid GetGuid() const { return m_guid; }
    uint32 GetAccountId() const { return m_accountId; }
    bool Initialize();
};

#endif // LOGINQUERYHOLDER_H