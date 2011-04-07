#include <limits>
#include <tf/transform_datatypes.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include "LineDetectionNode.hpp"

using visualization_msgs::Marker;
using visualization_msgs::MarkerArray;

// TODO: Convert the direction of principal curvature to real-world coordinates.

LineDetectionNode::LineDetectionNode(ros::NodeHandle nh, std::string ground_id,
                                     bool debug)
	: m_debug(debug),
	  m_invert(false),
	  m_valid(false),
	  m_num_prev(0),
	  m_ground_id(ground_id),
	  m_nh(nh),
	  m_it(nh)
{
	m_sub_cam = m_it.subscribeCamera("image", 1, &LineDetectionNode::ImageCallback, this);
	m_pub_max = m_it.advertise("line_maxima", 10);
	m_pub_pts = m_nh.advertise<PointCloud>("line_points", 10);

	if (m_debug) {
		ROS_WARN("debugging topics are enabled; performance may be degraded");

		m_pub_pre        = m_it.advertise("line_pre",        10);
		m_pub_distance   = m_it.advertise("line_distance",   10);
		m_pub_ker_hor    = m_it.advertise("line_kernel_hor", 10);
		m_pub_ker_ver    = m_it.advertise("line_kernel_ver", 10);
		m_pub_filter_hor = m_it.advertise("line_filter_hor", 10);
		m_pub_filter_ver = m_it.advertise("line_filter_ver", 10);
		m_pub_visual_one = m_nh.advertise<Marker>("/visualization_marker", 1);
	}
}

void LineDetectionNode::SetCutoffWidth(int width_cutoff)
{
	ROS_ASSERT(width_cutoff > 0);

	m_valid        = m_valid && (width_cutoff == m_width_cutoff);
	m_width_cutoff = width_cutoff;
}

void LineDetectionNode::SetDeadWidth(double width_dead)
{
	ROS_ASSERT(width_dead > 0.0);

	m_valid      = m_valid && (width_dead == m_width_dead);
	m_width_dead = width_dead;
}

void LineDetectionNode::SetLineWidth(double width_line)
{
	ROS_ASSERT(width_line > 0.0);

	m_valid      = m_valid && (width_line == m_width_line);
	m_width_line = width_line;
}

void LineDetectionNode::SetInvert(bool invert) {
	m_invert = invert;
}

void LineDetectionNode::SetGroundPlane(Plane plane)
{
	m_valid = m_valid && (plane.point.x == m_plane.point.x)
	                  && (plane.point.y == m_plane.point.y)
	                  && (plane.point.z == m_plane.point.z)
	                  && (plane.normal.x == m_plane.normal.x)
	                  && (plane.normal.y == m_plane.normal.y)
	                  && (plane.normal.z == m_plane.normal.z)
	                  && (plane.forward.x == m_plane.forward.x)
	                  && (plane.forward.y == m_plane.forward.y)
	                  && (plane.forward.z == m_plane.forward.z);
	m_plane = plane;
}

void LineDetectionNode::SetThreshold(double threshold)
{
	m_valid     = m_valid && (threshold == m_threshold);
	m_threshold = threshold;
}

void LineDetectionNode::SetResolution(int width, int height)
{
	ROS_ASSERT(width > 0 && height > 0);

	m_valid = m_valid && (width == m_cols) && (height == m_rows);
	m_cols  = width;
	m_rows  = height;
}

void LineDetectionNode::NonMaxSupr(cv::Mat src_hor, cv::Mat src_ver,
                                   std::list<cv::Point2i> &dst)
{
	ROS_ASSERT(src_hor.rows == src_ver.rows && src_hor.cols == src_ver.cols);
	ROS_ASSERT(src_hor.type() == CV_64FC1 && src_ver.type() == CV_64FC1);
	ROS_ASSERT(m_valid);

	for (int y = 1; y < src_hor.rows - 1; ++y)
	for (int x = 1; x < src_hor.cols - 1; ++x) {
		double val_hor   = src_hor.at<double>(y, x);
		double val_left  = src_hor.at<double>(y, x - 1);
		double val_right = src_hor.at<double>(y, x + 1);

		double val_ver = src_ver.at<double>(y + 0, x);
		double val_top = src_ver.at<double>(y - 1, x);
		double val_bot = src_ver.at<double>(y + 1, x);

		bool is_hor = val_hor > val_left && val_hor > val_right && val_hor > m_threshold;
		bool is_ver = val_ver > val_top  && val_ver > val_bot   && val_ver > m_threshold;

		if (is_hor || is_ver) {
			dst.push_back(cv::Point2i(x, y));
		}
	}
}

void LineDetectionNode::UpdateCache(void)
{
	static cv::Point3d const dhor(1.0, 0.0, 0.0);
	static cv::Point3d const dver(0.0, 0.0, 1.0);

	ROS_ASSERT(m_width_line > 0.0);
	ROS_ASSERT(m_width_dead > 0.0);
	ROS_ASSERT(m_width_cutoff > 0);

	if (!m_valid) {
		ROS_INFO("rebuilding cache with changed parameters");
		ROS_INFO("found ground plane P(%4f, %4f, %4f) N(%4f, %f, %f)",
			m_plane.point.x,  m_plane.point.y,  m_plane.point.z,
			m_plane.normal.x, m_plane.normal.y, m_plane.normal.z
		);

		m_horizon_hor = GeneratePulseFilter(dhor, m_kernel_hor, m_offset_hor);
		m_horizon_ver = GeneratePulseFilter(dhor, m_kernel_ver, m_offset_ver);
		//m_horizon_ver = GeneratePulseFilter(dver, m_kernel_ver, m_offset_ver);

		ROS_INFO("detected horizon horizontal = %d, vertical = %d",
			m_horizon_hor, m_horizon_ver
		);
	}
	m_valid = true;
}

void LineDetectionNode::ImageCallback(ImageConstPtr const &msg_img,
                                      CameraInfoConstPtr const &msg_cam)
{
	// Keep the ground plane in sync with the latest TF data.
	Plane plane;
	try {
		std::string ground_id = m_ground_id;
		std::string camera_id = msg_img->header.frame_id;

		GuessGroundPlane(m_tf, ground_id, camera_id, plane);
	} catch (tf::TransformException ex) {
		ROS_ERROR_THROTTLE(30, "%s", ex.what());
		return;
	}

	// Convert ROS messages to OpenCV data types.
	cv_bridge::CvImagePtr img_ptr;
	cv::Mat img_input;

	try {
		img_ptr = cv_bridge::toCvCopy(msg_img, image_encodings::BGR8);
	} catch (cv_bridge::Exception &e) {
		ROS_ERROR_THROTTLE(30, "%s", e.what());
		return;
	}

	// FIXME: Flush the cache when camerainfo changes.
	m_model.fromCameraInfo(msg_cam);
	img_input = img_ptr->image;

	// Update pre-computed values that were cached (only if necessary!).
	SetGroundPlane(plane);
	SetResolution(msg_img->width, msg_img->height);
	UpdateCache();

	// Processing...
	std::list<cv::Point2i> maxima;
	cv::Mat img_hor, img_ver;
	cv::Mat img_pre;

	LineColorTransform(img_input, img_pre, m_invert);
	PulseFilter(img_pre, img_hor, m_kernel_hor, m_offset_hor, true);
	PulseFilter(img_pre, img_ver, m_kernel_ver, m_offset_ver, false);
	NonMaxSupr(img_hor, img_ver, maxima);

	// Publish a three-dimensional point cloud in the camera frame by converting
	// each maximum's pixel coordinates to camera coordinates using the camera's
	// intrinsics and knowledge of the ground plane.
	// TODO: Scrap the std::list middleman.
	// TODO: Do this directly in NonMaxSupr().
	// TODO: Precompute the mapping from 2D to 3D.
	PointCloud::Ptr msg_pts(new PointCloud);
	std::list<cv::Point2i>::iterator it;

	// Use a row vector to store unordered points (as per PCL documentation).
	msg_pts->header.stamp    = msg_img->header.stamp;
	msg_pts->header.frame_id = msg_img->header.frame_id;
	msg_pts->height   = img_input.rows;
	msg_pts->width    = img_input.cols;
	msg_pts->is_dense = true;
	msg_pts->points.resize(img_input.rows * img_input.cols);

	for (int y = 0; y < img_input.rows; ++y)
	for (int x = 0; x < img_input.cols; ++x) {
		pcl::PointXYZ &pt = msg_pts->points[y * img_input.cols + x];
		pt.x = std::numeric_limits<double>::quiet_NaN();
		pt.y = std::numeric_limits<double>::quiet_NaN();
		pt.z = std::numeric_limits<double>::quiet_NaN();
	}

	int i;
	for (it = maxima.begin(), i = 0; it != maxima.end(); ++it, ++i) {
		cv::Point3d    pt_world = GetGroundPoint(*it);
		pcl::PointXYZ &pt_cloud = msg_pts->points[it->y * img_input.cols + it->x];
		pt_cloud.x = pt_world.x;
		pt_cloud.y = pt_world.y;
		pt_cloud.z = pt_world.z;
	}

	m_pub_pts.publish(msg_pts);

	// Two dimensional local maxima as a binary image. Detected line pixels are
	// white (255) and all other pixels are black (0).
	cv::Mat img_max(img_input.rows, img_input.cols, CV_8U, cv::Scalar(0));
	for (it = maxima.begin(); it != maxima.end(); ++it) {
		img_max.at<uint8_t>(it->y, it->x) = 255;
	}

	cv_bridge::CvImage msg_max;
	msg_max.header.stamp    = msg_img->header.stamp;
	msg_max.header.frame_id = msg_img->header.frame_id;
	msg_max.encoding = image_encodings::MONO8;
	msg_max.image    = img_max;
	m_pub_max.publish(msg_max.toImageMsg());
	if (m_debug) {
		// Preprocessing output.
		cv::Mat img_pre_8u;
		img_pre.convertTo(img_pre_8u, CV_8UC1);

		cv_bridge::CvImage msg_pre;
		msg_pre.header.stamp    = msg_img->header.stamp;
		msg_pre.header.frame_id = msg_img->header.frame_id;
		msg_pre.encoding = image_encodings::MONO8;
		msg_pre.image    = img_pre_8u;
		m_pub_pre.publish(msg_pre.toImageMsg());

		// Render lines every 1 m on the ground plane and render them in 3D!
		cv::Mat img_distance  = img_input.clone();
		cv::Point3d P_ground  = m_plane.point;
		cv::Point3d P_forward = m_plane.forward;
		P_forward *= 1.0 / sqrt(P_forward.dot(P_forward));

		double z_step = 1;
		double z_max  = 1000;

		Marker msg_contour;
		msg_contour.header.stamp    = msg_img->header.stamp;
		msg_contour.header.frame_id = msg_img->header.frame_id;
		msg_contour.ns     = "line_contour";
		msg_contour.id     = 0;
		msg_contour.type   = Marker::LINE_LIST;
		msg_contour.action = Marker::ADD;
		msg_contour.points.resize(2 * z_max);
		msg_contour.scale.x = 0.05;
		msg_contour.color.r = 1.0;
		msg_contour.color.g = 0.0;
		msg_contour.color.b = 0.0;
		msg_contour.color.a = 1.0;

		for (int i = 0; i < (int)(z_max / z_step); ++i) {
			P_ground += z_step * P_forward;
			cv::Point2d p = m_model.project3dToPixel(P_ground);
			cv::Point2d p1(0.0, p.y);
			cv::Point2d p2(m_cols, p.y);
			cv::line(img_distance, cv::Point2d(0.0, p.y), cv::Point2d(m_cols, p.y), cv::Scalar(255, 0, 0), 1);

			// 3D Marker
			msg_contour.points[2 * i + 0].x = P_ground.x - 1.0;
			msg_contour.points[2 * i + 0].y = P_ground.y;
			msg_contour.points[2 * i + 0].z = P_ground.z;
			msg_contour.points[2 * i + 1].x = P_ground.x + 1.0;
			msg_contour.points[2 * i + 1].y = P_ground.y;
			msg_contour.points[2 * i + 1].z = P_ground.z;
		}

		cv_bridge::CvImage msg_distance;
		msg_distance.header.stamp    = msg_img->header.stamp;
		msg_distance.header.frame_id = msg_img->header.frame_id;
		msg_distance.encoding = image_encodings::RGB8;
		msg_distance.image    = img_distance;
		m_pub_distance.publish(msg_distance.toImageMsg());
		m_pub_visual_one.publish(msg_contour);

		// Visualize the matched pulse width kernels.
		cv::Mat img_ker_hor;
		cv::normalize(m_kernel_hor, img_ker_hor, 0, 255, CV_MINMAX, CV_8UC1);

		cv_bridge::CvImage msg_ker_hor;
		msg_ker_hor.header.stamp    = msg_img->header.stamp;
		msg_ker_hor.header.frame_id = msg_img->header.frame_id;
		msg_ker_hor.encoding = image_encodings::MONO8;
		msg_ker_hor.image    = img_ker_hor;
		m_pub_ker_hor.publish(msg_ker_hor.toImageMsg());

		cv::Mat img_ker_ver;
		cv::normalize(m_kernel_ver, img_ker_ver, 0, 255, CV_MINMAX, CV_8UC1);

		cv_bridge::CvImage msg_ker_ver;
		msg_ker_ver.header.stamp    = msg_img->header.stamp;
		msg_ker_ver.header.frame_id = msg_img->header.frame_id;
		msg_ker_ver.encoding = image_encodings::MONO8;
		msg_ker_ver.image    = img_ker_ver;
		m_pub_ker_ver.publish(msg_ker_ver.toImageMsg());

		// Visualize the raw filter responses.
		cv::Mat img_filter_hor;
		cv::normalize(img_hor, img_filter_hor, 0, 255, CV_MINMAX, CV_8UC1);

		cv_bridge::CvImage msg_filter_hor;
		msg_filter_hor.header.stamp    = msg_img->header.stamp;
		msg_filter_hor.header.frame_id = msg_img->header.frame_id;
		msg_filter_hor.encoding = image_encodings::MONO8;
		msg_filter_hor.image    = img_filter_hor;
		m_pub_filter_hor.publish(msg_filter_hor.toImageMsg());

		cv::Mat img_filter_ver;
		cv::normalize(img_ver, img_filter_ver, 0, 255, CV_MINMAX, CV_8UC1);

		cv_bridge::CvImage msg_filter_ver;
		msg_filter_ver.header.stamp    = msg_img->header.stamp;
		msg_filter_ver.header.frame_id = msg_img->header.frame_id;
		msg_filter_ver.encoding = image_encodings::MONO8;
		msg_filter_ver.image    = img_filter_ver;
		m_pub_filter_ver.publish(msg_filter_ver.toImageMsg());
	}
}

cv::Point3d LineDetectionNode::GetGroundPoint(cv::Point2d pt)
{
	cv::Point3d ray     = m_model.projectPixelTo3dRay(pt);
	cv::Point3d &normal = m_plane.normal;
	cv::Point3d &plane  = m_plane.point;
	return (plane.dot(normal) / ray.dot(normal)) * ray;
}

double LineDetectionNode::ProjectDistance(cv::Point2d pt, cv::Point3d offset)
{
	// Project the expected edge points back into the image.
	cv::Point3d P = GetGroundPoint(pt);
	cv::Point2d p1 = m_model.project3dToPixel(P);
	cv::Point2d p2 = m_model.project3dToPixel(P + offset);

	// Find the distance between the reprojected points.
	cv::Point2d diff = p2 - p1;
	return sqrt(diff.dot(diff));
}

double LineDetectionNode::ReprojectDistance(cv::Point2d pt, cv::Point2d offset)
{
	cv::Point3d P1 = GetGroundPoint(pt);
	cv::Point3d P2 = GetGroundPoint(pt + offset);

	cv::Point3d diff = P2 - P1;
	return sqrt(diff.dot(diff));
}

int LineDetectionNode::GeneratePulseFilter(cv::Point3d dw, cv::Mat &kernel, std::vector<Offset> &offsets)
{
	static Offset const offset_template = { 0, 0 };

	ROS_ASSERT(m_rows > 0 && m_cols > 0);
	ROS_ASSERT(m_width_line > 0.0);
	ROS_ASSERT(m_width_dead > 0.0);

	kernel.create(m_rows, m_cols, CV_64FC1);
	kernel.setTo(0.0);

	offsets.clear();
	offsets.resize(m_rows, offset_template);

	int width_prev = INT_MAX;
	int horizon    = m_rows - 1;

	for (int r = m_rows - 1; r >= 0; --r) {
		cv::Point2d middle(m_cols / 2, r);
		int offs_line_neg = ProjectDistance(middle, -0.5 * m_width_line * dw);
		int offs_line_pos = ProjectDistance(middle, +0.5 * m_width_line * dw);
		int offs_both_neg = ProjectDistance(middle, -0.5 * (m_width_line + 2.0 * m_width_dead) * dw);
		int offs_both_pos = ProjectDistance(middle, +0.5 * (m_width_line + 2.0 * m_width_dead) * dw);

		int width_line = offs_line_neg + offs_line_pos;
		int width_both = offs_both_neg + offs_both_pos;
		int width_dead = width_both - width_line;
		int width_min  = std::min(width_line, width_dead);

		// Only generate a kernel when both the filter's pulse and supports are
		// larger than the cutoff size. This guarantees that the filter is not
		// degenerate and will sum to zero.
		if (m_width_cutoff <= width_min && width_prev >= width_min) {
			cv::Range row(r, r + 1);
			cv::Mat left   = kernel(row, cv::Range(0,                             offs_both_neg - offs_line_neg));
			cv::Mat center = kernel(row, cv::Range(offs_both_neg - offs_line_neg, offs_both_neg + offs_line_pos));
			cv::Mat right  = kernel(row, cv::Range(offs_both_neg + offs_line_pos, offs_both_neg + offs_both_pos));

			double value_left   = -0.5 / left.cols;
			double value_center = +1.0 / center.cols;
			double value_right  = -0.5 / right.cols;

			left.setTo(value_left);
			center.setTo(value_center);
			right.setTo(value_right);

			offsets[r].neg = offs_both_neg;
			offsets[r].pos = offs_both_pos;
			horizon        = r;

		} else {
			return horizon + 1;
		}
		width_prev = width_min;
	}
	return 0;
}

void LineDetectionNode::PulseFilter(cv::Mat src, cv::Mat &dst, cv::Mat ker,
                                    std::vector<Offset> const &offsets,
                                    bool horizontal)
{
	ROS_ASSERT(src.type() == CV_64FC1);
	ROS_ASSERT(ker.type() == CV_64FC1);
	ROS_ASSERT(ker.rows == src.rows && ker.cols == ker.cols);
	ROS_ASSERT((int)offsets.size() == ker.rows);

	dst.create(src.rows, src.cols, CV_64FC1);
	dst.setTo(std::numeric_limits<double>::quiet_NaN());

	for (int r = m_rows - 1; r >= 0; --r) {
		Offset const &offset = offsets[r];

		// Select the pre-computed kernel for this row.
		cv::Range ker_rows(r, r + 1);
		cv::Range ker_cols(0, offset.neg + offset.pos);
		cv::Mat ker_chunk = ker(ker_rows, ker_cols);

		for (int c = m_cols - 1; c >= 0; --c) {
			// At or above the horizon line.
			if (offset.pos == 0 && offset.neg == 0) break;

			// Select the region of the source image to convolve with the
			// kernel. This may not be centered on (c, r) due to the distance
			// distortion caused by perspective projection.
			cv::Mat src_chunk;

			if (horizontal) {
				cv::Range src_rows(r, r + 1);
				cv::Range src_cols(c - offset.neg, c + offset.pos);

				if (src_cols.start >= 0 && src_cols.end <= m_cols) {
					src_chunk = src(src_rows, src_cols);
				} else {
					continue;
				}
			} else {
				cv::Range src_rows(r - offset.neg, r + offset.pos);
				cv::Range src_cols(c, c + 1);

				if (src_rows.start >= 0 && src_rows.end <= m_rows) {
					src_chunk = src(src_rows, src_cols).t();
				} else {
					continue;
				}
			}

			dst.at<double>(r, c) = src_chunk.dot(ker_chunk);
		}
	}
}
