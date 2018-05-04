#include <string>
#include <iostream>
#include <ros/ros.h>
#include <nav_msgs/OccupancyGrid.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_ros/impl/transforms.hpp>
#include <pcl_ros/segmentation/sac_segmentation.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl_ros/transforms.h>
#include <math.h>
//#include <pcl/PCLPointCloud2.h>

using actor_cloud_t = pcl::PointCloud<pcl::PointXYZ>;
using actor_cloud_ptr_t = actor_cloud_t::Ptr;

/*PCloud2GMap
 *
 * Converts the PointCloud2 data from the velodyne Lidar
 *  into a grid_map
 *
 * Publish:     grid_map on "/grid_map"
 *
 * Subscrivbe:  PointCloud2 on "/velodyne_points"
 */
class PCloud2GMap {

public:
    PCloud2GMap();

private:
    void TransformToBase(actor_cloud_ptr_t& out, actor_cloud_ptr_t& in);
    bool WithinThreshold(const float& x, const float& y, const float& z );

    // Callback
    void CloudCallback(const sensor_msgs::PointCloud2ConstPtr&);

    ros::NodeHandle nh_;
    ros::Publisher  pub_;
    ros::Subscriber sub_;

    float z_max; //the roof of the car
    float x_min;
    float x_max;
    float width; 
};

// constructor
//  set up publisher and subscriber
PCloud2GMap::PCloud2GMap()
{
    //We only care about points within 
    // Z E {0,..,z_max}
    // X E {x_min,...,x_max}
    // Y E {-width/2,...,width/2}
    z_max = 2.0f; //TODO add to dynamic reconfigure
    x_min = 1.0f; //TODO add to dynamic reconfigure
    x_max = 5.0f; //TODO add to dynamic reconfigure
    width = 2.0f; //TODO add to dynamic reconfigure 

    nh_ = ros::NodeHandle("~");
    // publish on ~/obstacle_loc
    //pub_ = nh_.advertise<geometry_msgs::Point>("/obstacle_loc", 10);
    pub_ = nh_.advertise<geometry_msgs::PointStamped>("/obstacle_loc", 10);
    // subscribe to the pointcloud2 data comming from the lidar
    sub_ = nh_.subscribe<sensor_msgs::PointCloud2>("/velodyne_points", 100, &PCloud2GMap::CloudCallback, this);
}

bool PCloud2GMap::WithinThreshold(const float& x, const float& y, const float& z ){

 
    if (z < z_max){
        if (x >= x_min && x <= x_max){
            if (y > -(width/2) && y < (width/2)){
                return true;
            }
        }
    }

    return false;
}

void PCloud2GMap::CloudCallback(const sensor_msgs::PointCloud2ConstPtr& msg)
{

    // velodyne publishes 
    // x float32
    // y float32
    // z float32
    // intesity float32
    // ring uint32


    pcl::PCLPointCloud2 pcl_pc2;
    //convert from msg to pcl
    pcl_conversions::toPCL(*msg,pcl_pc2);

    //create input cloud
    actor_cloud_ptr_t cloud(new pcl::PointCloud<pcl::PointXYZ>);
    
    //convert from pcl to ros
    pcl::fromPCLPointCloud2(pcl_pc2,*cloud);

    //create cloud for transforming
    actor_cloud_ptr_t centeredCloud(new pcl::PointCloud<pcl::PointXYZ>);

    //The ground plane and rotation may not be needed.
    //It seems like the puck is able to do it for us.    
    TransformToBase(cloud,centeredCloud);

    geometry_msgs::PointStamped closest_point;

    closest_point.header = msg->header;
    closest_point.point.x = INT_MAX;

    double dist = INT_MAX;

    for (auto p : *cloud){
        if (WithinThreshold(p.x, p.y, p.z)){
            double dist2 =  sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
            if (dist > dist2){
                dist = dist2;
                closest_point.point.x = p.x;
                closest_point.point.y = p.y;
                closest_point.point.z = p.z;
            }
        }
    }

    ROS_DEBUG("Point: %f", dist);

    pub_.publish(closest_point);
}


void PCloud2GMap::TransformToBase(actor_cloud_ptr_t& out, actor_cloud_ptr_t& in)
{
    //find ground plane (http://pointclouds.org/documentation/tutorials/planar_segmentation.php)
    pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients());
    pcl::PointIndices::Ptr inliers(new pcl::PointIndices());

    // Create the segmentation object
    pcl::SACSegmentation<pcl::PointXYZ> seg;
    // Optional
    seg.setOptimizeCoefficients (true);
    // Mandatory
    seg.setModelType (pcl::SACMODEL_PLANE);
    seg.setMethodType (pcl::SAC_RANSAC);
    seg.setDistanceThreshold (0.01);

    seg.setInputCloud (in);
    seg.segment(*inliers, *coefficients);

    //(https://stackoverflow.com/questions/32299903/rotation-and-transformation-of-a-plane-to-xy-plane-and-origin-in-pointcloud)

    Eigen::Matrix<float, 1, 3> floor_plane_normal_vector, xy_plane_normal_vector;

    //The normal vector for the base plane (floor plane)
    //generated from the segmentation
    floor_plane_normal_vector[0] = coefficients->values[0];
    floor_plane_normal_vector[1] = coefficients->values[1];
    floor_plane_normal_vector[2] = coefficients->values[2];

    //NORMAL of the XY plane <0,0,1> (aka the Z axis)
    xy_plane_normal_vector[0] = 0.0;
    xy_plane_normal_vector[1] = 0.0;
    xy_plane_normal_vector[2] = 1.0;

    
    Eigen::Vector3f rotation_vector = xy_plane_normal_vector.cross(floor_plane_normal_vector);
    float theta = -atan2(rotation_vector.norm(), xy_plane_normal_vector.dot(floor_plane_normal_vector));

    Eigen::Affine3f transform_2 = Eigen::Affine3f::Identity();
    transform_2.rotate (Eigen::AngleAxisf (theta, rotation_vector));
    transform_2.translation() << 0, 0, 30; //is 30 correct?
    pcl::transformPointCloud (*in, *out, transform_2);

}

int
main(int argc, char **argv)
{
    ros::init(argc, argv, "pointcloud2gridmap");
    PCloud2GMap PCloud2GMap;

    ros::spin();
}