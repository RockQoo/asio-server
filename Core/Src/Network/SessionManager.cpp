#include "Core/Src/pch.h"
#include "Core/Src/Network/SessionManager.h"
#include "Core/Src/Network/Session.h"

namespace Network
{
    void SessionManager::Add(const std::shared_ptr<Session>& session)
    {
        std::unique_lock lock(mutex_);
        sessions_.emplace(session->Id(), session);
    }

    void SessionManager::Remove(const SessionId id)
    {
        std::unique_lock lock(mutex_);
        sessions_.erase(id);
    }

    std::shared_ptr<Session> SessionManager::Find(const SessionId id) const
    {
        std::shared_lock lock(mutex_);
        const auto it = sessions_.find(id);
        return it != sessions_.end() ? it->second : nullptr;
    }

    size_t SessionManager::Count() const
    {
        std::shared_lock lock(mutex_);
        return sessions_.size();
    }

    void SessionManager::ForEach(const std::function<void(const std::shared_ptr<Session>&)>& func) const
    {
        std::shared_lock lock(mutex_);
        for (const auto& [id, session] : sessions_)
        {
            func(session);
        }
    }
}
