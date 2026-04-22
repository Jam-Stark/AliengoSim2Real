root@noetic-gpu:/work# ls
AliengoSim2Real  unitree_legged_sdk
root@noetic-gpu:/work# cd unitree_legged_sdk/
root@noetic-gpu:/work/unitree_legged_sdk# git submodule update --init --recursive
fatal: detected dubious ownership in repository at '/work/unitree_legged_sdk'
To add an exception for this directory, call:

        git config --global --add safe.directory /work/unitree_legged_sdk
root@noetic-gpu:/work/unitree_legged_sdk# git config --global --add safe.directory /work/unitree_legged_sdk
root@noetic-gpu:/work/unitree_legged_sdk# git submodule update --init --recursive
root@noetic-gpu:/work/unitree_legged_sdk# git submodule update --init --recursive
root@noetic-gpu:/work/unitree_legged_sdk# source /opt/ros/noetic/setup.bash
root@noetic-gpu:/work/unitree_legged_sdk# mkdir -p /root/catkin_ws/src
root@noetic-gpu:/work/unitree_legged_sdk# ln -sf /work/projects/unitree_ros_to_real/unitree_legged_msgs /root/catkin_ws/src/
root@noetic-gpu:/work/unitree_legged_sdk# ln -sf /work/projects/unitree_ros_to_real/unitree_legged_real /root/catkin_ws/src/
root@noetic-gpu:/work/unitree_legged_sdk# ln -sf /work/projects/unitree_ros_to_real/unitree_legged_sdk  /root/catkin_ws/src/
root@noetic-gpu:/work/unitree_legged_sdk# ln -sf /work/projects/AliengoSim2Real/ros1                    /root/catkin_ws/src/aliengo_deploy
root@noetic-gpu:/work/unitree_legged_sdk# cd /root/catkin_ws
root@noetic-gpu:~/catkin_ws# catkin_make -DCMAKE_BUILD_TYPE=Release
Base path: /root/catkin_ws
Source space: /root/catkin_ws/src
Build space: /root/catkin_ws/build
Devel space: /root/catkin_ws/devel
Install space: /root/catkin_ws/install
Creating symlink "/root/catkin_ws/src/CMakeLists.txt" pointing to "/opt/ros/noetic/share/catkin/cmake/toplevel.cmake"
####
#### Running command: "cmake /root/catkin_ws/src -DCMAKE_BUILD_TYPE=Release -DCATKIN_DEVEL_PREFIX=/root/catkin_ws/devel -DCMAKE_INSTALL_PREFIX=/root/catkin_ws/install -G Unix Makefiles" in "/root/catkin_ws/build"
####
-- The C compiler identification is GNU 9.4.0
-- The CXX compiler identification is GNU 9.4.0
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Using CATKIN_DEVEL_PREFIX: /root/catkin_ws/devel
-- Using CMAKE_PREFIX_PATH: /opt/ros/noetic
-- This workspace overlays: /opt/ros/noetic
-- Found PythonInterp: /usr/bin/python3 (found suitable version "3.8.10", minimum required is "3") 
-- Using PYTHON_EXECUTABLE: /usr/bin/python3
-- Using Debian Python package layout
-- Found PY_em: /usr/lib/python3/dist-packages/em.py  
-- Using empy: /usr/lib/python3/dist-packages/em.py
-- Using CATKIN_ENABLE_TESTING: ON
-- Call enable_testing()
-- Using CATKIN_TEST_RESULTS_DIR: /root/catkin_ws/build/test_results
-- Forcing gtest/gmock from source, though one was otherwise available.
-- Found gtest sources under '/usr/src/googletest': gtests will be built
-- Found gmock sources under '/usr/src/googletest': gmock will be built
-- Found PythonInterp: /usr/bin/python3 (found version "3.8.10") 
-- Found Threads: TRUE  
-- Using Python nosetests: /usr/bin/nosetests3
-- catkin 0.8.12
-- BUILD_SHARED_LIBS is on
-- BUILD_SHARED_LIBS is on
-- Configuring done
-- Generating done
-- Build files have been written to: /root/catkin_ws/build
####
#### Running command: "make -j32 -l32" in "/root/catkin_ws/build"
####
root@noetic-gpu:~/catkin_ws# source devel/setup.bashrospack find aliengo_deploy
bash: devel/setup.bashrospack: No such file or directory
root@noetic-gpu:~/catkin_ws# rospack find unitree_legged_real
[rospack] Error: package 'unitree_legged_real' not found