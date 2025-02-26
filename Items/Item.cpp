#include "Item.h"
#include "Beer.h"
#include "Cigarette.h"
#include "Handcuffs.h"
#include "Handsaw.h"
#include "MagnifyingGlass.h"
#include <string>

std::unique_ptr<Item> Item::createByName(std::string_view name) {
  if (name == "Beer")
    return std::make_unique<Beer>();
  else if (name == "Cigarette")
    return std::make_unique<Cigarette>();
  else if (name == "Handcuffs")
    return std::make_unique<Handcuffs>();
  else if (name == "Handsaw")
    return std::make_unique<Handsaw>();
  else if (name == "Magnifying Glass")
    return std::make_unique<MagnifyingGlass>();

  return nullptr;
}