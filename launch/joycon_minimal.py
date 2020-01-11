from launch import LaunchDescription
from launch.actions import Shutdown
from ament_index_python.packages import get_package_prefix
import launch_ros.actions
package = "minirys_drivers"

def generate_launch_description():
	path = get_package_prefix(package)
	path_list = path.split('/')
	del path_list[-2]
	path_list.insert(-1, 'src')
	full_path = '/'.join(path_list)

	return LaunchDescription([
		launch_ros.actions.Node(
			# the name of the executable is set in CMakeLists.txt, towards the end of
			# the file, in add_executable(...) and the directives following it
			package= package,
			node_executable='temperature_publisher.py',
			output='screen',
			parameters=[full_path + "/yaml/temperature_publisher_params.yaml"]
		),

		launch_ros.actions.Node(
			# the name of the executable is set in CMakeLists.txt, towards the end of
			# the file, in add_executable(...) and the directives following it
			package= package,
			node_executable='voltage_publisher.py',
			output='screen',
			parameters=[full_path + "/yaml/voltage_publisher_params.yaml"]
		),

		launch_ros.actions.Node(
			# the name of the executable is set in CMakeLists.txt, towards the end of
			# the file, in add_executable(...) and the directives following it
			package= package,
			node_executable='joycon_control_only',
			output='screen',
			# env={RCUTILS_CONSOLE_LINE_BUFFERED: "1"},
			# env={PYTHONUNBUFFERED : "1"},
			# emulate_tty=True,
			# arguments=[('__log_level:=info')],
			parameters=[full_path + "/yaml/joycon_control_only_params.yaml"],
			on_exit=Shutdown(),
		),
	])
