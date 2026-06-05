root@a2-humble:/work/projects/AliengoSim2Real/ros2# A2/scripts/a2_real_robot_test.sh connected-preflight enp131s0
[a2-real-test] log: /tmp/a2_real_robot_tests/connected_preflight_20260605_133656.log
pwd=/work/projects/AliengoSim2Real/ros2
ROS_DISTRO=humble
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
A2_NET_IFACE=enp131s0
A2_LOWSTATE_TOPIC=/lowstate
A2_LOWCMD_TOPIC=/lowcmd
CYCLONEDDS_URI=<CycloneDDS><Domain Id="any"><General><Interfaces><NetworkInterface name="enp131s0" priority="default" multicast="default" /></Interfaces></General></Domain></CycloneDDS>
2: enp131s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UP group default qlen 1000
    link/ether 34:5a:60:88:34:5e brd ff:ff:ff:ff:ff:ff
    inet 192.168.123.222/24 brd 192.168.123.255 scope global noprefixroute enp131s0
       valid_lft forever preferred_lft forever
    inet6 fe80::eeef:538f:9e35:fff9/64 scope link noprefixroute 
       valid_lft forever preferred_lft forever
PING 192.168.123.161 (192.168.123.161) 56(84) bytes of data.
64 bytes from 192.168.123.161: icmp_seq=1 ttl=64 time=0.344 ms
64 bytes from 192.168.123.161: icmp_seq=2 ttl=64 time=0.244 ms
64 bytes from 192.168.123.161: icmp_seq=3 ttl=64 time=0.244 ms
64 bytes from 192.168.123.161: icmp_seq=4 ttl=64 time=0.228 ms
64 bytes from 192.168.123.161: icmp_seq=5 ttl=64 time=0.213 ms

--- 192.168.123.161 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 4099ms
rtt min/avg/max/mdev = 0.213/0.254/0.344/0.046 ms
/api/assistant_recorder/request
/api/assistant_recorder/response
/api/audiohub/request
/api/audiohub/response
/api/bashrunner/request
/api/bashrunner/response
/api/basic_demarcate/request
/api/basic_demarcate/response
/api/basic_demarcate_lease/request
/api/basic_demarcate_lease/response
/api/config/request
/api/config/response
/api/fourg_agent/request
/api/fourg_agent/response
/api/gas_sensor/request
/api/gas_sensor/response
/api/gpt/request
/api/gpt/response
/api/loco/request
/api/loco/response
/api/motion_switcher/request
/api/motion_switcher/response
/api/obstacles_avoid/request
/api/obstacles_avoid/response
/api/pet/request
/api/pet/response
/api/programming_actuator/request
/api/programming_actuator/response
/api/rm_con/request
/api/robot_state/request
/api/robot_state/response
/api/robot_type_service/request
/api/robot_type_service/response
/api/sport/request
/api/sport/response
/api/sport_lease/request
/api/sport_lease/response
/api/uwbswitch/request
/api/uwbswitch/response
/api/videohub/request
/api/videohub/response
/api/voice/request
/api/voice/response
/api/vui/request
/api/vui/response
/arm_Command
/arm_Feedback
/audio_msg
/audiohub/player/state
/audiosender
/config_change_status
/frontvideostream
/gas_sensor
/gpt_cmd
/gpt_state
/gptflowfeedback
/lf/battery_alarm
/lf/bmsstate
/lf/emergency_stop
/lf/lowstate
/lf/mainboardstate
/lf/secondary_bmsstate
/lf/secondary_imu
/lf/sportmodestate
/lio_sam_ros2/mapping/odometry
/log_system_inbound
/log_system_outbound
/lowcmd
/lowstate
/lowstate_raw
/multiplestate
/parameter_events
/pctoimage_local
/pet/flowfeedback
/programming_actuator/command
/programming_actuator/feedback
/public_network_status
/qt_add_edge
/qt_add_node
/qt_command
/qt_notice
/query_result_edge
/query_result_node
/rosout
/rtc/state
/rtc_status
/secondary_imu
/selftest
/servicestate
/servicestateactivate
/sportmodestate
/uslam/client_command
/uslam/frontend/cloud_world_ds
/uslam/frontend/odom
/uslam/localization/cloud_world
/uslam/localization/odom
/uslam/navigation/global_path
/uslam/server_log
/utlidar/range_info
/utlidar/robot_pose
/utlidar/switch
/utlidar/voxel_map_compressed
/uwbstate
/uwbswitch
/videohub/inner
/webrtcreq
/webrtcres
/wirelesscontroller
/wirelesscontroller_unprocessed
/xfk_webrtcreq
/xfk_webrtcres
Type: unitree_hg/msg/LowState

Publisher count: 1

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowState
Endpoint type: PUBLISHER
GID: 01.10.80.37.55.34.7f.e5.67.7b.32.5a.00.00.02.03.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: RELIABLE
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Subscription count: 2

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowState
Endpoint type: SUBSCRIPTION
GID: 01.10.42.84.79.a6.5a.ad.13.94.75.ae.00.00.08.04.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: BEST_EFFORT
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowState
Endpoint type: SUBSCRIPTION
GID: 01.10.b7.62.c3.c0.27.3c.1b.b8.c7.b9.00.00.06.04.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: BEST_EFFORT
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Type: ['unitree_go/msg/LowState', 'unitree_hg/msg/LowState']

Publisher count: 1

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowState
Endpoint type: PUBLISHER
GID: 01.10.80.37.55.34.7f.e5.67.7b.32.5a.00.00.06.03.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: RELIABLE
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Subscription count: 2

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowState
Endpoint type: SUBSCRIPTION
GID: 01.10.65.e4.e2.70.b9.c3.bc.ef.b8.88.00.00.58.04.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: RELIABLE
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_go/msg/LowState
Endpoint type: SUBSCRIPTION
GID: 01.10.93.d9.1a.8e.a7.f6.98.f1.75.20.00.00.02.04.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: BEST_EFFORT
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Type: unitree_hg/msg/LowCmd

Publisher count: 1

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowCmd
Endpoint type: PUBLISHER
GID: 01.10.42.84.79.a6.5a.ad.13.94.75.ae.00.00.02.03.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: RELIABLE
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Subscription count: 1

Node name: _CREATED_BY_BARE_DDS_APP_
Node namespace: _CREATED_BY_BARE_DDS_APP_
Topic type: unitree_hg/msg/LowCmd
Endpoint type: SUBSCRIPTION
GID: 01.10.80.37.55.34.7f.e5.67.7b.32.5a.00.00.0e.04.00.00.00.00.00.00.00.00
QoS profile:
  Reliability: BEST_EFFORT
  History (Depth): KEEP_LAST (1)
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

uint32[2] version
uint8 mode_pr
uint8 mode_machine
uint32 tick
IMUState imu_state
        float32[4] quaternion
        float32[3] gyroscope
        float32[3] accelerometer
        float32[3] rpy
        int16 temperature
MotorState[35] motor_state
        uint8 mode
        float32 q
        float32 dq
        float32 ddq
        float32 tau_est
        int16[2] temperature
        float32 vol
        uint32[2] sensor
        uint32 motorstate
        uint32[4] reserve
uint8[40] wireless_remote
uint32[4] reserve
uint32 crc
uint8 mode_pr
uint8 mode_machine

MotorCmd[35] motor_cmd
        uint8 mode
        float32 q
        float32 dq
        float32 tau
        float32 kp
        float32 kd
        uint32 reserve

uint32[4] reserve
uint32 crc