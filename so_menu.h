#ifndef MENU_H
#define MENU_H
FunctionOption functionOption = FUNCTION_OPTION_NONE;  //菜单选项

enum MenuOption {
  MENU_OPTION_NONE,
  MENU_OPTION_1,
  MENU_OPTION_2,
  MENU_OPTION_3
};

enum FunctionOption {
  FUNCTION_OPTION_NONE,
  FUNCTION_CALIBRATION,
  FUNCTION_SET_SAMPLE,
  FUNCTION_SET_CONTAINER,
  FUNCTION_SHOW_INFO,  
  FUNCTION_EXTRATION
};


#endif