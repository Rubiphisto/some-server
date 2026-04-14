#pragma once

namespace Common
{
    template<class T>
    struct CreateUsingNew
    {
        static T* Create() { return new T; }
        static void Destroy(const T* instance) { delete instance; }
    };

    template<typename T, template<class> class Creator = CreateUsingNew>
    class Singleton
    {
    public:
        static void Create() { sInstance = Creator<T>::Create(); }
        static void Destroy()
        {
            Creator<T>::Destroy(sInstance);
            sInstance = nullptr;
        }
        static T& Instance() { return *sInstance; }
    private:
        static T* sInstance;
    };
    template<typename T, template<class> class C>
    T* Singleton<T, C>::sInstance = nullptr;
}