#include "../header/utility.h"

void fatal_error(std::string error_message){
  perror(std::string("Fatal Error: " + error_message).c_str());
  exit(1);
}