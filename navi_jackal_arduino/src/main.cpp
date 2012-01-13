#include <util/atomic.h>
#include <WProgram.h>

#include <ros.h>
#include <navi_jackal/ControlConstants.h>
#include <navi_jackal/EncoderTicks.h>
#include <navi_jackal/VelocitySetpoint.h>
#include <std_msgs/Int32.h>
#include <std_msgs/Float32.h>

#include "config.hpp"
#include "encoder.hpp"
#include "motor.hpp"
#include "pid.hpp"
#include "main.hpp"

using navi_jackal::EncoderTicks;
using navi_jackal::ControlConstants;
using navi_jackal::VelocitySetpoint;

static void update_constants(ControlConstants const &msg_consts);
static void update_setpoints(VelocitySetpoint const &msg_setpoints);

static EncoderTicks msg_encoders;

static ros::NodeHandle nh;
static ros::Publisher pub_encoders("encoders", &msg_encoders);
static ros::Subscriber<ControlConstants> sub_constants("constants", &update_constants);
static ros::Subscriber<VelocitySetpoint> sub_setpoints("setpoint", &update_setpoints);

void setup(void)
{
	encoder_init();
	motor_init();
	pid_init();

	motor_enable(1);

	nh.initNode();
	nh.advertise(pub_encoders);
	nh.subscribe(sub_constants);
	nh.subscribe(sub_setpoints);
}

void loop(void)
{
	ATOMIC_BLOCK (ATOMIC_FORCEON) {
		msg_encoders.ticks_left  = encoder1_buffer;
		msg_encoders.ticks_right = encoder2_buffer;
		encoder1_buffer = 0;
		encoder2_buffer = 0;
	}
	pub_encoders.publish(&msg_encoders);
	nh.spinOnce();
	delay(100);
}

static void update_constants(ControlConstants const &msg_constants)
{
	pid_set_constants(
		msg_constants.feedforward,
		msg_constants.proportional,
		msg_constants.integral,
		msg_constants.derivative,
		msg_constants.threshold
	);
}

static void update_setpoints(VelocitySetpoint const &msg_setpoints)
{
	// TODO
}
