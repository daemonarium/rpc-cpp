#pragma once

#include <iostream>
#include <sstream>
#include <vector>
#include <memory>

struct Occupation {
  std::string name;
  int years;

  Occupation() : name(""), years(0) {}
  Occupation(std::string namep, int yearsp) : name(namep), years(yearsp) {}
};

struct Skill {
  std::string name;
  int level;
};

struct Person {
  std::string name;
  int age;

  Occupation occupation;

  std::vector<Skill> skills;
  std::vector<std::shared_ptr<Person>> friends;

  Person() : name(""), age(0), occupation(), skills() {}
  Person(std::string namep, int agep, Occupation occupationp, std::initializer_list<Skill> skillsp) : name(namep), age(agep), occupation(occupationp), skills(skillsp) {}
};

void print_people(const std::vector<std::shared_ptr<Person>>& people) {
  for (const auto& person : people) { 
    std::cout << "Person:" << std::endl;
    std::cout << "  Name: " << person->name << ", Age: " << person->age << std::endl;
    std::cout << "  Occupation: " << person->occupation.name << ", Years: " << person->occupation.years << std::endl;
    std::cout << "  Skills: " << std::endl;
    for (const auto& skill : person->skills) {
      std::cout << "    " << skill.name << ", Level: " << skill.level << std::endl;
    }
    std::cout << "  Friends: " << std::endl;
    for (const auto& fd : person->friends) {
      if(fd) {
        std::cout << "    " << fd->name << std::endl;
      } else {
        std::cout << "    nullptr" << std::endl;
      }
    }
  }
}

