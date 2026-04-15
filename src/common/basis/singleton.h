#pragma once

#include <cassert>
#include <memory>
#include <utility>

namespace Common
{
    template<class T>
    struct CreateUsingNew
    {
        template<class... Args>
        static T* Create(Args&&... args) { return new T(std::forward<Args>(args)...); }

        static void Destroy(T* instance) { delete instance; }
    };

    template<typename T, template<class> class Creator = CreateUsingNew>
    class Singleton
    {
    public:
        Singleton() = delete;
        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;

        template<class... Args>
        static T& Create(Args&&... args)
        {
            if (!sInstance)
            {
                sInstance.reset(Creator<T>::Create(std::forward<Args>(args)...));
            }

            assert(sInstance && "Singleton instance creation failed");
            return *sInstance;
        }

        static bool Exists() noexcept { return static_cast<bool>(sInstance); }

        static T* TryInstance() noexcept { return sInstance.get(); }

        static T* Ptr() noexcept { return sInstance.get(); }

        static void Destroy() noexcept
        {
            if (sInstance)
            {
                Creator<T>::Destroy(sInstance.release());
            }
        }

        static T& Instance() noexcept
        {
            assert(sInstance && "Singleton instance has not been created");
            return *sInstance;
        }

    private:
        struct InstanceDeleter
        {
            void operator()(T* instance) const noexcept
            {
                Creator<T>::Destroy(instance);
            }
        };

        static std::unique_ptr<T, InstanceDeleter> sInstance;
    };

    template<typename T, template<class> class C>
    std::unique_ptr<T, typename Singleton<T, C>::InstanceDeleter> Singleton<T, C>::sInstance = nullptr;
}
