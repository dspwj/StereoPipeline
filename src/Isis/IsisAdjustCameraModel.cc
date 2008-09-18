#include <vw/Cartography/PointImageManipulation.h>
#include <Isis/IsisAdjustCameraModel.h>

// Isis Headers
#include <Cube.h>
#include <Camera.h>
#include <CameraDetectorMap.h>
#include <CameraDistortionMap.h>
#include <CameraFocalPlaneMap.h>

using namespace vw;
using namespace vw::camera;

//-------------------------------------------------------------------------
//  Constructors / Deconstructor
//-------------------------------------------------------------------------
IsisAdjustCameraModel::IsisAdjustCameraModel( std::string cube_filename,
			   boost::shared_ptr<VectorEquation> position_func,
			   boost::shared_ptr<QuaternionEquation> pose_func ) : IsisCameraModel( cube_filename ),
      m_position_func( position_func ),
      m_pose_func( pose_func ) {

  m_position_func->set_time_offset( (m_max_ephemeris + m_min_ephemeris)/2 );
  m_pose_func->set_time_offset( (m_max_ephemeris + m_min_ephemeris)/2 );
	
}

IsisAdjustCameraModel::~IsisAdjustCameraModel() {
}

//-------------------------------------------------------------------------
//  Traditional Camera Routines
//-------------------------------------------------------------------------

Vector2 IsisAdjustCameraModel::point_to_pixel( Vector3 const& point) const {
  std::cout << "Warning: Using broken implementation" << std::endl;

  // This is broken because this routine doesn't using the equations
  // that perform the adjustment. I haven't worked out how to do this
  // and a least-squares method seems inevitable. If you can, use the
  // point_to_mm_time method.

  return IsisCameraModel::point_to_pixel( point );
}

Vector3 IsisAdjustCameraModel::pixel_to_vector( Vector2 const& pix ) const {
  Vector3 mm_time = this->pixel_to_mm_time( pix );
  
  return this->mm_time_to_vector( mm_time );
}

Vector3 IsisAdjustCameraModel::camera_center( Vector2 const& pix )  const {
  Vector3 mm_time = this->pixel_to_mm_time( pix );

  return this->camera_center( mm_time );
}

Quaternion<double> IsisAdjustCameraModel::camera_pose( Vector2 const& pix ) const {
  Vector3 mm_time = this->pixel_to_mm_time( pix );
  
  return this->camera_pose( mm_time );
}

int IsisAdjustCameraModel::getLines( void ) const {
  return IsisCameraModel::getLines();
}

int IsisAdjustCameraModel::getSamples( void ) const {
  return IsisCameraModel::getSamples();
}

//-------------------------------------------------------------------------
//  Non-Traditional Camera Routines
//-------------------------------------------------------------------------

Vector3 IsisAdjustCameraModel::pixel_to_mm_time( Vector2 const& pix ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_image( pix[0], pix[1] );

  // Now finding the undistorted mms that make up the pixels
  Isis::CameraDistortionMap* distortMap = cam->DistortionMap();
  return Vector3( distortMap->UndistortedFocalPlaneX(),
		  distortMap->UndistortedFocalPlaneY(),
		  cam->EphemerisTime() );
}

Vector3 IsisAdjustCameraModel::point_to_mm_time( Vector3 const& mm_time, Vector3 const& point ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_time( mm_time[2] );

  // Finding the focal length for the camera in millimeters
  Isis::CameraDistortionMap* distortMap = cam->DistortionMap();
  double focal_length_mm = distortMap->UndistortedFocalPlaneZ();

  // Now building a pinhole camera model
  Vector3 center = this->camera_center( mm_time );
  Quaternion<double> orientation = this->camera_pose( mm_time );
  PinholeModel pin_cam( center, transpose(orientation.rotation_matrix()), 
			focal_length_mm, focal_length_mm,
			0, 0 );
  pin_cam.set_coordinate_frame( Vector3( 1, 0, 0 ),
				Vector3( 0, 1, 0 ),
				Vector3( 0, 0, 1 ) );
 

  // Performing the forward projection
  Vector2 forward_projection = pin_cam.point_to_pixel( point );
  return Vector3( forward_projection[0], forward_projection[1],
		  mm_time[2] );
}

Vector3 IsisAdjustCameraModel::mm_time_to_vector( Vector3 const& mm_time ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_time( mm_time[2] );

  // Now looking up the focal length
  Isis::CameraDistortionMap* distortMap = cam->DistortionMap();
  Vector3 pointing( mm_time[0], mm_time[1], distortMap->UndistortedFocalPlaneZ() );

  // Normalize
  pointing = normalize( pointing );

  Quaternion<double> look_transform = this->camera_pose( mm_time );
  return inverse( look_transform ).rotate( pointing );
}

Vector3 IsisAdjustCameraModel::camera_center( Vector3 const& mm_time ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_time( mm_time[2] );

  // Remember that Isis performs globe centered globe fixed
  // calculations in kilometers.
  double pos[3];
  cam->InstrumentPosition( pos );
  return Vector3( pos[0]*1000, pos[1]*1000, pos[2]*1000 ) + m_position_func->evaluate( mm_time[2] );
}

Quaternion<double> IsisAdjustCameraModel::camera_pose( Vector3 const& mm_time ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_time( mm_time[2] );
  
  // Convert from instrument frame-> J2000 frame --> Globe centered-fixed
  std::vector<double> rot_inst = cam->InstrumentRotation()->Matrix();
  std::vector<double> rot_body = cam->BodyRotation()->Matrix();
  MatrixProxy<double,3,3> R_inst(&(rot_inst[0]));
  MatrixProxy<double,3,3> R_body(&(rot_body[0]));

  return Quaternion<double>(R_inst*inverse(R_body)) + m_pose_func->evaluate( mm_time[2] );
}

double IsisAdjustCameraModel::undistorted_focal( Vector3 const& mm_time ) const {
  Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
  this->set_time( mm_time[2] );

  // Looking up the focal length
  Isis::CameraDistortionMap* distortMap = cam->DistortionMap();
      
  return distortMap->UndistortedFocalPlaneZ();
}

//-------------------------------------------------------------------------
// Protected
//-------------------------------------------------------------------------

void IsisAdjustCameraModel::set_image( double const& sample, double const& line) const {
  if (m_current_line != line || m_current_sample != sample ) {
    Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
    cam->SetImage( sample, line );
    m_current_line = cam->Line();
    m_current_sample = cam->Sample();
    m_current_time = cam->EphemerisTime();
  }
}

void IsisAdjustCameraModel::set_time( double const& time ) const {
  if (time > m_max_ephemeris || time < m_min_ephemeris)
    std::cout << "Time input is out of bounds" << std::endl;
  if (m_current_time != time ) {
    Isis::Camera* cam = static_cast<Isis::Camera*>( m_isis_camera_ptr );
    cam->SetEphemerisTime( time );
    m_current_line = cam->Line();
    m_current_sample = cam->Sample();
    m_current_time = cam->EphemerisTime();
  }
}
