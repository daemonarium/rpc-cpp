#pragma once

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

class GameObject {
  public:
  size_t id;
  virtual void update() = 0;
};

class Player : public GameObject {
  public:
  std::string name = "Player";
  virtual void update() override {
    std::cout << "Player update" << std::endl;
  }
};

class Enemy : public GameObject {
  public:
  int level = 1;
  virtual void update() override {
    std::cout << "Enemy update" << std::endl;
  }
};
