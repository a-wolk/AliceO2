//
// Created by arczipt on 01.06.22.
//

#ifndef O2_MYSERVICE_H
#define O2_MYSERVICE_H

#include <fairlogger/Logger.h>

namespace o2::framework
{
    class MyService {
    public:
        MyService() {};
        void print(const std::string& text);

    private:
        int counter = 0;
    };
}

#endif //O2_MYSERVICE_H
