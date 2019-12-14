#include <iostream>
#include "CLifeGame.cpp"

//////////////////////////////////////

int main(int argc, char* argv[]) {
    CLifeGame* lifeGame = CLifeGame::get_instance();
    lifeGame->Start(5, 5, "random");
    lifeGame->Status();
    lifeGame->Run(1);
    lifeGame->Status();
    sleep(2);
    lifeGame->Status();
    sleep(2);
    lifeGame->Status();
    sleep(2);
    lifeGame->Status();
    return 0;
}
