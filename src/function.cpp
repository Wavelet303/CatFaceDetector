#include "function.hpp"

void Function::detectSingleImage( std::string src_path, std::string dst_path )
{
  bool show = dst_path.compare("") == 0 ? true : false;
  std::string imgname = src_path.substr( src_path.find_last_of('/')+1 );
  std::string write_path = dst_path + "/" + imgname;

  std::cout << "Detection on a single image: " << src_path << std::endl;

  if( show )    std::cout << "Showing detection result" << std::endl;
  else          std::cout << "Writing detection result to " << write_path << std::endl;

  Detector detector;
  detector.loadModels( "models/cat_face.cfg", "models/cat_features.cfg" );

  cv::Mat image = cv::imread( src_path );
  cv::Mat org = image.clone();
  std::vector<cv::Rect> detections = detector.detect( image );

  Detector::drawDetections( image, detections );

  if( show )
  {
    cv::namedWindow( "original", cv::WINDOW_NORMAL );
    cv::resizeWindow( "original", 800, 800 );
    cv::imshow( "original", org );

    cv::namedWindow( "detection", cv::WINDOW_NORMAL );
    cv::resizeWindow( "detection", 800, 800 );
    cv::imshow( "detection", image );
    cv::waitKey(0);
  }
  else
  {
    cv::imwrite( write_path, image );
  }
}

void Function::detectMultipleImages( std::string src_path, std::string dst_path )
{
  bool show = dst_path.compare("") == 0 ? true : false;

  std::cout << "Detection on multiple images in: " << src_path << std::endl;

  std::vector<boost::filesystem::path> paths = getImagePathsInFolder( src_path, ".jpg" );

  std::cout << "Found " << paths.size() << " images in the folder" << std::endl;

  Detector detector;
  detector.loadModels( "models/cat_face.cfg", "models/cat_features.cfg" );

  std::cout << std::endl << std::endl;

  for( std::vector<boost::filesystem::path>::iterator it = paths.begin(); it != paths.end(); ++it )
  {
    std::string imgpath = src_path + "/" + (*it).string();
    std::cout << "Reading from " << imgpath << std::endl;

    cv::Mat image = cv::imread( imgpath );
    cv::Mat org = image.clone();
    std::vector<cv::Rect> detections = detector.detect( image );

    Detector::drawDetections( image, detections );

    if( show )
    {
      cv::namedWindow( "original", cv::WINDOW_AUTOSIZE );
      cv::resizeWindow( "original", 800, 800 );
      cv::imshow( "original", org );

      cv::namedWindow( "detection", cv::WINDOW_AUTOSIZE );
      cv::resizeWindow( "detection", 800, 800 );
      cv::imshow( "detection", image );
      cv::waitKey(0);
    }
    else
    {
      std::string write_path = dst_path + "/" + (*it).string();
      cv::imwrite( write_path, image );
    }
  }
}

void Function::detectVideo( std::string src_path, std::string dst_path, bool use_tracking )
{
  boost::posix_time::ptime t1, t2;
  boost::posix_time::time_duration dur;

  bool show = dst_path.compare("") == 0 ? true : false;

  cv::VideoCapture cap( src_path );
  cv::VideoWriter writer;

  if( !cap.isOpened() )
  {
    std::cout << "Cannot open " << src_path << std::endl;
    return;
  }

  double fps = cap.get( CV_CAP_PROP_FPS );

  std::cout << "Detection on video: " << src_path << std::endl;

  Detector detector;
  detector.loadModels( "models/cat_face.cfg", "models/cat_features.cfg" );

  if( show )
  {
    cv::namedWindow( "video", cv::WINDOW_NORMAL );
    cv::resizeWindow( "video", 800, 800 );
  }
  else
  {
    std::string videoname = src_path.substr( src_path.find_last_of('/')+1 );
    std::string write_path = dst_path + "/" + videoname;

    std::cout << "Writing detection result to: " << write_path << std::endl;

    cv::Mat sample;
    cap >> sample;

    if( !sample.data )
    {
      std::cout << "ERROR! first frame is blank" << std::endl;
      return;
    }

    int codec = CV_FOURCC( 'M', 'J', 'P', 'G' );
    writer.open( write_path, codec, fps, sample.size(), true );

    if( !writer.isOpened() )
    {
      std::cout << "ERROR! could not open " << dst_path << " for writing" << std::endl;
      return;
    }
  }

  std::vector<cv::Ptr<cv::Tracker> > trackers(NUM_FEATURES,cv::TrackerKCF::create());
  bool isInit[NUM_FEATURES] = {false};

  int count = 0;
  int detection_rate = fps;

  double total_tracking_time = 0.0;
  int total_tracking_count = 0;

  while( true )
  {
    std::cout << "frame" << count << std::endl;
    cv::Mat frame;
    cap.read(frame);
    cv::Mat clone = frame.clone();
    // cap >> frame;

    if( !frame.data )  break;

    std::vector<cv::Rect> detections(NUM_FEATURES, cv::Rect());

    if( use_tracking )
    {
      if( count % detection_rate == 0 )
      {
        detections = detector.detect( frame );

        for( int i = FACE+1; i < trackers.size(); i++ )
        {
          if( detections[i].area() > 0 )
          {
            if( count > 0 )
            {
              trackers[i]->clear();
            }
            trackers[i] = cv::TrackerMIL::create();
            trackers[i]->init(frame, detections[i]);
            isInit[i] = true;
          }
          else
          {
            isInit[i] = false;
          }
        }
      }
      else
      {
        t1 = boost::posix_time::microsec_clock::local_time();

        //pthread
        pthread_t pthreads[NUM_FEATURES-1];
        for( int i = FACE+1; i < trackers.size(); i++ ) {
          updateTrackingThreadArgs args;
          args.tracker = trackers[i];
          args.detection = &detections[i];
          args.isInit = isInit[i];
          // std::cout << "1" << std::endl;
          args.frame = &clone;
          // std::cout << "2" << std::endl;
          struct updateTrackingThreadArgs *ptr = (struct updateTrackingThreadArgs *)calloc(1, sizeof( struct updateTrackingThreadArgs ) );
          // std::cout << "3" << std::endl;
          *ptr = args;
          // std::cout << "4" << std::endl;
          if( pthread_create(pthreads+i-1, NULL, updateTrackingThread, ptr)) {
            std::cout << "Thread creation failed!" << std::endl;
            return;
          }
        }

        for( int i = FACE+1; i < trackers.size(); i++ ) {
          if( pthread_join(pthreads[i-1],NULL) ) {
            std::cout << "Thread join failed!" << std::endl;
            return;
          }
        }

        // sequential
        // for( int i = FACE+1; i < trackers.size(); i++ )
        // {
        //   if( isInit[i] )
        //   {
        //     cv::Rect2d updated_rect_2d;
        //     trackers[i]->update( frame, updated_rect_2d );
        //     cv::Rect updated_rect( (int)updated_rect_2d.x, (int)updated_rect_2d.y, (int)updated_rect_2d.width, (int)updated_rect_2d.height );
        //     detections[i] = updated_rect;
        //   }
        // }
        
        t2 = boost::posix_time::microsec_clock::local_time();
        dur = t2 - t1;
        int dur_milli = dur.total_milliseconds();
        std::cout << "Tracking took: " << dur_milli << " ms" << std::endl;

        total_tracking_time += dur_milli;
        total_tracking_count++;
      }
    }
    else
    {
      detections = detector.detect( frame );
    }

    Detector::drawDetections( frame, detections );

    if( show )
    {
      cv::imshow( "video", frame );
      cv::waitKey(1);
      // cv::waitKey(0);
    }
    else
    {
      writer.write( frame );
    }

    count++;
  }

  if( use_tracking ) {
    std::cout << "Average tracking time per frame: " << total_tracking_time/total_tracking_count << " ms" << std::endl;
  }
}

void *Function::updateTrackingThread(void *ptr) {
  // std::cout << "6" << std::endl;
  updateTrackingThreadArgs args = *(struct updateTrackingThreadArgs *) ptr;

  if( args.isInit )
  {
    cv::Rect2d updated_rect_2d;
    args.tracker->update( *(args.frame), updated_rect_2d );
    cv::Rect updated_rect( (int)updated_rect_2d.x, (int)updated_rect_2d.y, (int)updated_rect_2d.width, (int)updated_rect_2d.height );
    *(args.detection) = updated_rect;
  }

  free(ptr);
  return NULL;
}

std::vector<boost::filesystem::path> Function::getImagePathsInFolder( const boost::filesystem::path &folder, const std::string &ext )
{
  std::vector<boost::filesystem::path>paths;

  if( !boost::filesystem::exists(folder) || !boost::filesystem::is_directory(folder) )  return paths;

  boost::filesystem::recursive_directory_iterator it(folder);
  boost::filesystem::recursive_directory_iterator endit;

  while( it != endit )
  {
    if( boost::filesystem::is_regular_file(*it) && it->path().extension() == ext ) paths.push_back( it->path().filename() );
    ++it;
  }

  return paths;
}