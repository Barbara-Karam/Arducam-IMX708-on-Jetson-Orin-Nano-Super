from setuptools import find_packages, setup

package_name = "imx708_camera_py"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages",
            ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="your name",
    maintainer_email="you@example.com",
    description="Arducam IMX708 ROS 2 publisher for Jetson Orin Nano.",
    license="Apache-2.0",
    entry_points={
        "console_scripts": [
            "imx708_publisher = imx708_camera_py.imx708_publisher:main",
        ],
    },
)
