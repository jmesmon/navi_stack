#include <jaus/mobility/sensors/localposesensor.h>
#include <jaus/mobility/sensors/velocitystatesensor.h>
#include <jaus/mobility/drivers/localwaypointlistdriver.h>
#include <jaus/core/transport/judp.h>
#include <jaus/core/component.h>

#include <tf/tf.h>

/* TODO: replace with tf or eigen */
#include <LinearMath/btQuaternion.h>
#include <LinearMath/btMatrix3x3.h>

#include <cxutils/time.h>

#include <ros/ros.h>
#include <nav_msgs/Odometery.h>

#include <navi_executive/AddWaypoint.h>

static JAUS::LocalPoseSensor *local_pose_sensor;
static JAUS::VelocityStateSensor *velocity_state_sensor;

void position_cb(nav_msgs::Odometery::Ptr odom)
{
    JAUS::LocalPose local_pose;
    local_pose->SetX(odom->pose.position.x);
    local_pose->SetY(odom->pose.position.y);
    local_pose->SetZ(odom->pose.position.z);

    CxUtils::Time cx_time(odom->header.stamp.toSec());
    local_pose->SetTimeStamp(cx_time);

    btQuaternion q;
    double r, p, y;
    tf::quaternionMsgToTF(msg->orientation, q);
    btMatrix3x3(q).getRPY(r, p, y);

    local_pose->SetRoll(r);
    local_pose->SetPitch(p);
    local_pose->SetYaw(y);

    local_pose_sensor->SetLocalPose(local_pose);


    JAUS::VelocityState velocity_state;

    velocity_state->SetVelocityX(odom->twist.linear.x);
    velocity_state->SetVelocityY(odom->twist.linear.y);
    velocity_state->SetVelocityZ(odom->twist.linear.z);

    velocity_state->SetVelocityRoll(odom->twist.angular.x);
    velocity_state->SetVelocityPitch(odom->twist.angular.y);
    velocity_state->SetVelocityYaw(odom->twist.angular.z);

    velocity_state->SetTimeStamp(cx_time);

    velocity_state_sensor->SetVelocityState(velocity_state);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "jaus");
    ros::NodeHandle nh;

    std::string odom_path;
    nh.getParam("~odom", odom_path, "/odom_fuse");

    JAUS::Component component;
    // Disable timeout. Normally, service would shutdown in 2 seconds.
    component.AccessControlService()->SetTimeoutPeriod(0);

    // WARNING: must be allocated via 'new'. JAUS::Component::Shutdown calls delete on this.
    local_pose_sensor = new JAUS::LocalPoseSensor();
    component.AddService(local_pose_sensor);

    // WARNING: must be allocated via 'new'. JAUS::Component::Shutdown calls delete on this.
    velocity_state_sensor = new JAUS::VelocityStateSensor();
    component.AddService(velocity_state_sensor);

    // WARNING: must be allocated via 'new'. JAUS::Component::Shutdown calls delete on this.
    component.AddService(new JAUS::ListManager());
    // WARNING: must be allocated via 'new'. JAUS::Component::Shutdown calls delete on this.
    JAUS::LocalWaypointListDriver* localWaypointListDriver = new JAUS::LocalWaypointListDriver();
    component.AddService(localWaypointListDriver);

    component.DiscoveryService()->SetSubsystemIdentification(JAUS::Subsystem::Vehicle, "navi");

    //Initialize JAUS, all components should be added at this time
    if(component.Initialize(JAUS::Address(ROBOT_SUBSYSTEM_ID, ROBOT_NODE_ID, ROBOT_COMPONENT_ID)) == false)
    {
        ROS_WARN("Failed to initialize JAUS");
        return 0;
    }

    // NOTE: muse run after local_pose_sensor and velocity_state_sensor created.
    ros::Subscriber position = nh.subscribe(odom_path, 1, position_cb);

    component.ManagementService()->SetStatus(JAUS::Management::Status::Standby);

    JAUS::JUDP *transportService = static_cast<JAUS:JUDP *>(component.TransportService());
    transportService->AddConnection(COP_IP_ADDR, JAUS::Address(COP_SUBSYSTEM_ID, COP_NODE_ID, COP_COMPONENT_ID));

    JAUS::Time::Stamp printTimeMs = 0;

    navi_executive::AddWaypoint add_waypoint;
    navi_executive::WaypointGPS waypoint;
    waypoint.lat = 0;
    waypoint.lon = 0;
    add_waypoint.waypoints.push(


    JAUS::Management management = component.ManagementService();

    while(ros::ok())
    {
        JAUS::Management::Status status = management->GetStatus();

        if (status == JAUS::Management::Status::Shutdown)
        {
            ROS_INFO("Shutdown receviced");
            break;
        }

        if (status == JAUS::Management::Status::Standby)
        {
            /* FIXME: what do we do durring standby? Do we need to look at the waypoint list at all? */
            ROS_INFO("JAUS Standby");
        }

#if 0
        // Get local waypoint list
        JAUS::Element::Map elementList = localWaypointListDriver->GetElementList();
        // Convert to SetLocalWaypointCommands
        JAUS::Element::Map::iterator listElement;
        std::vector<JAUS::SetLocalWaypoint> commandList;
        for(listElement = elementList.begin(); listElement != elementList.end(); listElement++)
        {
            if(listElement->second.mpElement->GetMessageCode() == JAUS::SET_LOCAL_WAYPOINT)
            {
                commandList.push_back(*((JAUS::SetLocalWaypoint *)(listElement->second.mpElement)) );
            }
        }
#endif

        //Execute 
        if(localWaypointListDriver->IsExecuting())
        {
            // TODO: Go through this, should be replace by executive
        }

        //XXX:Everything below is correct except TODO                                                                                           

        //Update Status
        if(JAUS::Time::GetUtcTimeMs() - printTimeMs > 5000)                                         
        {                                                                                          
            // Print status of services.                                                           
            std::cout << "\n=======================Basic Service Status============================\n";             
            component.AccessControlService()->PrintStatus(); std::cout << std::endl;               
            component.ManagementService()->PrintStatus(); std::cout << std::endl;                  
            //globalPoseSensor->PrintStatus(); std::cout << std::endl;                               
            local_pose_sensor->PrintStatus(); std::cout << std::endl;                                
            velocity_state_sensor->PrintStatus(); std::cout << std::endl;                            
            localWaypointListDriver->PrintStatus();                                                
            printTimeMs = JAUS::Time::GetUtcTimeMs();                                              
        }                                                                                          

        // Exit if escape key pressed.                                                             
        if(CxUtils::GetChar() == 27)                                                               
        {                                                                                          
            break;                                                                                 
        }                                                                                          


        CxUtils::SleepMs(250);
        ros::spinOnce();
    }                                                                                              
    //END MAIN JAUS LOOP---------------------------------------------------------------------------//
                                                                                                   
    // Shutdown any components associated with our subsystem.                                  
    component.Shutdown();                                                   

    return 0;
}
