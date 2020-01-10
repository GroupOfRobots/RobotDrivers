#include <memory>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <string>
#include <csignal>
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/battery_state.hpp"
#include "sensor_msgs/msg/temperature.hpp"
#include "bcm/bcm2835.h"
#include "l6470/motors.h"

#define JOY_DEV "/dev/input/js0"

using std::placeholders::_1;
using namespace std::chrono_literals;

bool endProcess = false;

void sigintHandler(int signum) {
	if (signum == SIGINT) {
		endProcess = true;
	}
}

class JoyconControl : public rclcpp::Node
{
	public:
		JoyconControl(): Node("joycon_control_only")
		{
			//initialize joycon
			if( ( joy_fd = open( JOY_DEV , O_RDONLY)) == -1 )
			{
				RCLCPP_ERROR(this->get_logger(), "Couldn't open joystick\n" );
				endProcess = true;
				return;
			}

			ioctl( joy_fd, JSIOCGAXES, &num_of_axis );
			ioctl( joy_fd, JSIOCGBUTTONS, &num_of_buttons );
			ioctl( joy_fd, JSIOCGNAME(80), &name_of_joystick );

			axis = (int *) calloc( num_of_axis, sizeof( int ) );
			axisPast = (int *) calloc( num_of_axis, sizeof( int ) );
			for(int i = 0; i < num_of_axis; i++){
				axis[i] = 0;
				axisPast[i] = 0;
			}
			button = (bool *) calloc( num_of_buttons, sizeof( bool ) );
			buttonPast = (bool *) calloc( num_of_buttons, sizeof( bool ) );
			for(int i = 0; i < num_of_buttons; i++){
				button[i] = false;
				buttonPast[i] = false;
			}

			RCLCPP_INFO(this->get_logger(),"Joystick detected: %s\n\t%d axis\n\t%d buttons\n\n"
			, name_of_joystick
			, num_of_axis
			, num_of_buttons );

			fcntl( joy_fd, F_SETFL, O_NONBLOCK ); /* use non-blocking mode */

			//initialize motors
			// board = &Motors( BCM2835_SPI_CS0, GPIO_RESET_OUT);
			board.setUp();
			board.resetPosition();
			this->declare_parameter("forwardAxis", rclcpp::ParameterValue(1));
			forwardAxis = this->get_parameter("forwardAxis").get_value<int>();
			this->declare_parameter("rotationAxis", rclcpp::ParameterValue(2));
			rotationAxis = this->get_parameter("rotationAxis").get_value<int>();
			this->declare_parameter("forwardSpeedFactor", rclcpp::ParameterValue(80));
			forwardSpeedFactor = this->get_parameter("forwardSpeedFactor").get_value<int>();
			this->declare_parameter("rotationSpeedFactor", rclcpp::ParameterValue(200));
			rotationSpeedFactor = this->get_parameter("rotationSpeedFactor").get_value<int>();
			this->declare_parameter("forwardAxisInverted", rclcpp::ParameterValue(false));
			forwardAxisInverted = this->get_parameter("forwardAxisInverted").get_value<bool>();
			this->declare_parameter("rotationAxisInverted", rclcpp::ParameterValue(false));
			rotationAxisInverted = this->get_parameter("rotationAxisInverted").get_value<bool>();

			//start subscriptions and timer
			battery_subscription = this->create_subscription<sensor_msgs::msg::BatteryState>(
			"vol", 10, std::bind(&JoyconControl::battery_callback, this, _1));

			temperature_subscription = this->create_subscription<sensor_msgs::msg::Temperature>(
			"tmp", 10, std::bind(&JoyconControl::temperature_callback, this, _1));

			rclcpp::ParameterValue default_period(10);
			rclcpp::ParameterValue period = this->declare_parameter("period", default_period);
			motor_control_timer = this->create_wall_timer(
			std::chrono::milliseconds(period.get<int>()), std::bind(&JoyconControl::motor_control, this));
		}

		~JoyconControl(){
			close( joy_fd );
			board.stop();
		}

  private:
		int joy_fd, *axis = NULL, *axisPast = NULL, num_of_axis = 0, num_of_buttons = 0;
		bool *button = NULL, *buttonPast = NULL;
		char name_of_joystick[80];
		struct js_event js;
		Motors board = Motors( BCM2835_SPI_CS0, GPIO_RESET_OUT);
		int speedLeft = 0, speedRight = 0, forwardAxis, rotationAxis, forwardSpeedFactor, rotationSpeedFactor;
		bool forwardAxisInverted, rotationAxisInverted;

		rclcpp::Subscription<sensor_msgs::msg::BatteryState>::SharedPtr battery_subscription;
		void battery_callback(const sensor_msgs::msg::BatteryState::SharedPtr msg) const
		{
			RCLCPP_INFO(this->get_logger(), "Current voltage: '%s'", std::to_string(msg->voltage).c_str());
			if (msg->power_supply_health == sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_DEAD) {
				RCLCPP_INFO(this->get_logger(), "Voltage too low, sending signal to close...");
				endProcess = true;
			}
		}

		rclcpp::Subscription<sensor_msgs::msg::Temperature>::SharedPtr temperature_subscription;
		void temperature_callback(const sensor_msgs::msg::Temperature::SharedPtr msg) const
		{
			RCLCPP_INFO(this->get_logger(), "Current temperature: '%s'", std::to_string(msg->temperature).c_str());
			if (msg->temperature > 60.0) {
				RCLCPP_INFO(this->get_logger(), "Temperature too high, sending signal to close...");
				endProcess = true;
			}
		}

		rclcpp::TimerBase::SharedPtr motor_control_timer;
		void motor_control()
		{
			/* read the joystick state */
			// read(joy_fd, &js, sizeof(struct js_event));

			/* see what to do with the event */
			while(read(joy_fd, &js, sizeof(struct js_event)) == sizeof(struct js_event)){
				switch (js.type & ~JS_EVENT_INIT)
				{
					case JS_EVENT_AXIS:
						axis   [ js.number ] = js.value;
						if (axis [ js.number ] != axisPast[js.number]) RCLCPP_INFO(this->get_logger(),"Axis %d:\t%d", js.number, axis[js.number]);
						axisPast[js.number] = axis[js.number];
						break;
					case JS_EVENT_BUTTON:
						button [ js.number ] = js.value;
						if (button [ js.number ] != buttonPast[js.number]) RCLCPP_INFO(this->get_logger(),"Button %d:\t%d", js.number, button[js.number]);
						buttonPast[js.number] = button[js.number];
						break;
				}
			}
			
			speedLeft = axis[forwardAxis]/forwardSpeedFactor;
			speedRight = axis[forwardAxis]/forwardSpeedFactor;
			if(forwardAxisInverted){
				speedLeft = -speedLeft;
				speedRight = -speedRight;
			}

			if(!rotationAxisInverted){
				speedLeft+= axis[rotationAxis]/rotationSpeedFactor;
				speedRight-= axis[rotationAxis]/rotationSpeedFactor;
			} else {
				speedLeft-= axis[rotationAxis]/rotationSpeedFactor;
				speedRight+= axis[rotationAxis]/rotationSpeedFactor;
			}

			board.setSpeed(speedLeft,speedRight);
		}
};

int main(int argc, char * argv[])
{
	setbuf(stdout, nullptr);
	signal(SIGINT, sigintHandler);
	if (bcm2835_init() == 0) {
		fprintf(stderr, "Not able to init the bmc2835 library\n");
		return -1;
	}
	rclcpp::init(argc, argv);
	auto node = std::make_shared<JoyconControl>();
	while(!endProcess) rclcpp::spin_some(node);
	rclcpp::shutdown();
	return 0;
}