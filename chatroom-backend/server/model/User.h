#pragma once

#include <string>

class User {
   public:
    User(int id = -1,
         std::string name = "",
         std::string email = "",
         std::string password = "",
         int state = 0);
    // getter
    int getId() const;
    std::string getName() const;
    std::string getEmail() const;
    std::string getPassword() const;
    int getState() const;
    // setter
    void setId(int id);
    void setName(const std::string& name);
    void setEmail(const std::string& email);
    void setPassword(const std::string& password);
    void setState(int state);

   private:
    int m_id;
    std::string m_name;
    std::string m_email;
    std::string m_password;
    int m_state;
};