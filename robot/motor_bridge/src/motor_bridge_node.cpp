#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

class MotorBridgeNode : public rclcpp::Node
{
public:
    MotorBridgeNode()
    : Node("motor_bridge_node"),
      serial_fd_(-1),
      rx_running_(false)
    {
        this->declare_parameter<std::string>("port", "/dev/ttyACM0");
        this->declare_parameter<int>("baudrate", 115200);
        this->declare_parameter<double>("wheel_separation", 0.30);
        this->declare_parameter<double>("wheel_radius", 0.05);
        this->declare_parameter<double>("max_wheel_speed", 1.0);
        this->declare_parameter<double>("watchdog_timeout", 0.5);

        port_ = this->get_parameter("port").as_string();
        baudrate_ = this->get_parameter("baudrate").as_int();
        wheel_separation_ = this->get_parameter("wheel_separation").as_double();
        wheel_radius_ = this->get_parameter("wheel_radius").as_double();
        max_wheel_speed_ = this->get_parameter("max_wheel_speed").as_double();
        watchdog_timeout_ = this->get_parameter("watchdog_timeout").as_double();

        feedback_pub_ = this->create_publisher<std_msgs::msg::String>("/motor_feedback", 10);

        cmd_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
            "/cmd_vel",
            10,
            std::bind(&MotorBridgeNode::cmdVelCallback, this, std::placeholders::_1)
        );

        last_cmd_time_ = this->now();

        if (openSerial(port_, baudrate_)) {
            RCLCPP_INFO(this->get_logger(), "Serial connected: %s (%d)", port_.c_str(), baudrate_);
        } else {
            RCLCPP_ERROR(this->get_logger(), "Failed to open serial port: %s", port_.c_str());
        }

        rx_running_ = true;
        rx_thread_ = std::thread(&MotorBridgeNode::serialReadLoop, this);

        watchdog_timer_ = this->create_wall_timer(
            100ms,
            std::bind(&MotorBridgeNode::watchdogCallback, this)
        );
    }

    ~MotorBridgeNode() override
    {
        rx_running_ = false;

        if (rx_thread_.joinable()) {
            rx_thread_.join();
        }

        closeSerial();
    }

private:
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr feedback_pub_;
    rclcpp::TimerBase::SharedPtr watchdog_timer_;

    std::string port_;
    int baudrate_;
    double wheel_separation_;
    double wheel_radius_;
    double max_wheel_speed_;
    double watchdog_timeout_;

    int serial_fd_;
    std::mutex serial_mutex_;
    std::thread rx_thread_;
    std::atomic<bool> rx_running_;
    rclcpp::Time last_cmd_time_;

    bool openSerial(const std::string& port, int baudrate)
    {
        serial_fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (serial_fd_ < 0) {
            return false;
        }

        struct termios tty;
        std::memset(&tty, 0, sizeof(tty));

        if (tcgetattr(serial_fd_, &tty) != 0) {
            close(serial_fd_);
            serial_fd_ = -1;
            return false;
        }

        speed_t speed;
        switch (baudrate) {
            case 9600:   speed = B9600; break;
            case 19200:  speed = B19200; break;
            case 38400:  speed = B38400; break;
            case 57600:  speed = B57600; break;
            case 115200: speed = B115200; break;
            default:
                close(serial_fd_);
                serial_fd_ = -1;
                return false;
        }

        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
        tty.c_iflag &= ~IGNBRK;
        tty.c_lflag = 0;
        tty.c_oflag = 0;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cflag &= ~(PARENB | PARODD);
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        tty.c_iflag &= ~(INLCR | ICRNL);
        tty.c_oflag &= ~(ONLCR | OCRNL);

        if (tcsetattr(serial_fd_, TCSANOW, &tty) != 0) {
            close(serial_fd_);
            serial_fd_ = -1;
            return false;
        }

        tcflush(serial_fd_, TCIOFLUSH);
        return true;
    }

    void closeSerial()
    {
        std::lock_guard<std::mutex> lock(serial_mutex_);
        if (serial_fd_ >= 0) {
            close(serial_fd_);
            serial_fd_ = -1;
        }
    }

    void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
    {
        const double v = msg->linear.x;
        const double w = msg->angular.z;

        double v_left = v - (wheel_separation_ / 2.0) * w;
        double v_right = v + (wheel_separation_ / 2.0) * w;

        v_left = clamp(v_left, -max_wheel_speed_, max_wheel_speed_);
        v_right = clamp(v_right, -max_wheel_speed_, max_wheel_speed_);

        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(3);
        oss << "CMD," << v_left << "," << v_right << "\n";

        sendSerial(oss.str());
        last_cmd_time_ = this->now();
    }

    void watchdogCallback()
    {
        const double dt = (this->now() - last_cmd_time_).seconds();
        if (dt > watchdog_timeout_) {
            sendSerial("CMD,0.000,0.000\n");
        }
    }

    void sendSerial(const std::string& data)
    {
        std::lock_guard<std::mutex> lock(serial_mutex_);

        if (serial_fd_ < 0) {
            return;
        }

        const ssize_t written = write(serial_fd_, data.c_str(), data.size());
        if (written < 0) {
            RCLCPP_ERROR(this->get_logger(), "Serial write failed");
        }
    }

    void serialReadLoop()
    {
        std::string line_buffer;
        char ch;

        while (rx_running_) {
            {
                std::lock_guard<std::mutex> lock(serial_mutex_);
                if (serial_fd_ >= 0) {
                    const ssize_t n = read(serial_fd_, &ch, 1);
                    if (n > 0) {
                        if (ch == '\n') {
                            if (!line_buffer.empty()) {
                                publishFeedback(line_buffer);
                                line_buffer.clear();
                            }
                        } else if (ch != '\r') {
                            line_buffer.push_back(ch);
                        }
                    }
                }
            }

            std::this_thread::sleep_for(2ms);
        }
    }

    void publishFeedback(const std::string& line)
    {
        std_msgs::msg::String msg;
        msg.data = line;
        feedback_pub_->publish(msg);
        RCLCPP_INFO(this->get_logger(), "RX: %s", line.c_str());
    }

    double clamp(double value, double min_val, double max_val)
    {
        if (value < min_val) return min_val;
        if (value > max_val) return max_val;
        return value;
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<MotorBridgeNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}