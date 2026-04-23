
root@noetic-gpu:~/catkin_ws# catkin_make -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -100
      |         ^~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:318:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘euler’
  318 |     cmd.euler[2] = 0;
      |         ^~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h: In function ‘unitree_legged_msgs::HighState state2rosMsg(UNITREE_LEGGED_SDK::HighState&)’:
/root/catkin_ws/src/unitree_legged_real/include/convert.h:255:33: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘head’
  255 |         ros_msg.head[i] = state.head[i];
      |                                 ^~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:256:35: error: invalid types ‘uint32_t {aka unsigned int}[int]’ for array subscript
  256 |         ros_msg.SN[i] = state.SN[i];
      |                                   ^
/root/catkin_ws/src/unitree_legged_real/include/convert.h:257:36: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘version’
  257 |         ros_msg.version[i] = state.version[i];
      |                                    ^~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:263:41: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘footForceEst’; did you mean ‘footForce’?
  263 |         ros_msg.footForceEst[i] = state.footForceEst[i];
      |                                         ^~~~~~~~~~~~
      |                                         footForce
/root/catkin_ws/src/unitree_legged_real/include/convert.h:264:42: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘rangeObstacle’
  264 |         ros_msg.rangeObstacle[i] = state.rangeObstacle[i];
      |                                          ^~~~~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:282:52: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘motorState’
  282 |         ros_msg.motorState[i] = state2rosMsg(state.motorState[i]);
      |                                                    ^~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:287:38: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘bms’
  287 |     ros_msg.bms = state2rosMsg(state.bms);
      |                                      ^~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:290:34: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘frameReserve’
  290 |     ros_msg.frameReserve = state.frameReserve;
      |                                  ^~~~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:293:30: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘progress’
  293 |     ros_msg.progress = state.progress;
      |                              ^~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:294:30: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘gaitType’
  294 |     ros_msg.gaitType = state.gaitType;
      |                              ^~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:295:37: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘footRaiseHeight’
  295 |     ros_msg.footRaiseHeight = state.footRaiseHeight;
      |                                     ^~~~~~~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:296:32: error: ‘struct UNITREE_LEGGED_SDK::HighState’ has no member named ‘bodyHeight’
  296 |     ros_msg.bodyHeight = state.bodyHeight;
      |                                ^~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/src/exe/twist_sub.cpp: In constructor ‘Custom::Custom()’:
/root/catkin_ws/src/unitree_legged_real/src/exe/twist_sub.cpp:29:33: error: invalid conversion from ‘const char*’ to ‘int’ [-fpermissive]
   29 |         low_udp(LOWLEVEL, 8091, "192.168.123.10", 8007),
      |                                 ^~~~~~~~~~~~~~~~
      |                                 |
      |                                 const char*
In file included from /root/catkin_ws/src/unitree_legged_sdk/include/unitree_legged_sdk/unitree_legged_sdk.h:10,
                 from /root/catkin_ws/src/unitree_legged_real/src/exe/twist_sub.cpp:6:
/root/catkin_ws/src/unitree_legged_sdk/include/unitree_legged_sdk/udp.h:25:53: note:   initializing argument 3 of ‘UNITREE_LEGGED_SDK::UDP::UDP(uint16_t, int, int, bool)’
   25 |         UDP(uint16_t localPort, int sendLength, int recvLength, bool isServer = false); // as server, client IP and port can change
      |                                                 ~~~~^~~~~~~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h: In function ‘UNITREE_LEGGED_SDK::HighCmd rosMsg2Cmd(const ConstPtr&)’:
/root/catkin_ws/src/unitree_legged_real/include/convert.h:308:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘head’
  308 |     cmd.head[0] = 0xFE;
      |         ^~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:309:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘head’
  309 |     cmd.head[1] = 0xEF;
      |         ^~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:314:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘footRaiseHeight’; did you mean ‘dFootRaiseHeight’?
  314 |     cmd.footRaiseHeight = 0;
      |         ^~~~~~~~~~~~~~~
      |         dFootRaiseHeight
/root/catkin_ws/src/unitree_legged_real/include/convert.h:315:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘bodyHeight’; did you mean ‘dBodyHeight’?
  315 |     cmd.bodyHeight = 0;
      |         ^~~~~~~~~~
      |         dBodyHeight
/root/catkin_ws/src/unitree_legged_real/include/convert.h:316:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘euler’
  316 |     cmd.euler[0] = 0;
      |         ^~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:317:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘euler’
  317 |     cmd.euler[1] = 0;
      |         ^~~~~
/root/catkin_ws/src/unitree_legged_real/include/convert.h:318:9: error: ‘struct UNITREE_LEGGED_SDK::HighCmd’ has no member named ‘euler’
  318 |     cmd.euler[2] = 0;
      |         ^~~~~
make[2]: *** [unitree_legged_real/CMakeFiles/ros_example_walk.dir/build.make:63: unitree_legged_real/CMakeFiles/ros_example_walk.dir/src/exe/example_walk.cpp.o] Error 1
make[1]: *** [CMakeFiles/Makefile2:2425: unitree_legged_real/CMakeFiles/ros_example_walk.dir/all] Error 2
make[2]: *** [unitree_legged_real/CMakeFiles/ros_example_position.dir/build.make:63: unitree_legged_real/CMakeFiles/ros_example_position.dir/src/exe/example_position.cpp.o] Error 1
make[1]: *** [CMakeFiles/Makefile2:2313: unitree_legged_real/CMakeFiles/ros_example_position.dir/all] Error 2
make[2]: *** [unitree_legged_real/CMakeFiles/twist_sub.dir/build.make:63: unitree_legged_real/CMakeFiles/twist_sub.dir/src/exe/twist_sub.cpp.o] Error 1
make[1]: *** [CMakeFiles/Makefile2:2145: unitree_legged_real/CMakeFiles/twist_sub.dir/all] Error 2
make[2]: *** [unitree_legged_real/CMakeFiles/ros_udp.dir/build.make:63: unitree_legged_real/CMakeFiles/ros_udp.dir/src/exe/ros_udp.cpp.o] Error 1
make[1]: *** [CMakeFiles/Makefile2:2201: unitree_legged_real/CMakeFiles/ros_udp.dir/all] Error 2
make[1]: *** [CMakeFiles/Makefile2:2369: unitree_legged_real/CMakeFiles/control_via_keyboard.dir/all] Error 2
make[1]: *** [CMakeFiles/Makefile2:2257: unitree_legged_real/CMakeFiles/state_sub.dir/all] Error 2
make: *** [Makefile:141: all] Error 2
Base path: /root/catkin_ws
Source space: /root/catkin_ws/src
Build space: /root/catkin_ws/build
Devel space: /root/catkin_ws/devel
Install space: /root/catkin_ws/install

#### Running command: "make cmake_check_build_system" in "/root/catkin_ws/build"

#### Running command: "make -j32 -l32" in "/root/catkin_ws/build"

Invoking "make -j32 -l32" failed
