// __BEGIN_LICENSE__
// Copyright (C) 2006-2009 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file stereo.cc
///
//#define USE_GRAPHICS

#include <asp/Tools/stereo.h>

namespace vw {
  template<> struct PixelFormatID<PixelMask<Vector<float, 5> > >   { static const PixelFormatEnum value = VW_PIXEL_GENERIC_6_CHANNEL; };
}

namespace vw {
  template<> struct PixelFormatID<Vector<float, 3> >   { static const PixelFormatEnum value = VW_PIXEL_GENERIC_3_CHANNEL; };
}

void print_usage(po::options_description const& visible_options) {
  vw_out() << "\nUsage: stereo [options] <Left_input_image> <Right_input_image> [Left_camera_file] [Right_camera_file] <output_file_prefix>\n"
           << "  Extensions are automaticaly added to the output files.\n"
           << "  Camera model arguments may be optional for some stereo session types (e.g. isis).\n"
           << "  Stereo parameters should be set in the stereo.default file.\n\n";
  vw_out() << visible_options << std::endl;
}

void print_image_format(ImageFormat const & imf) {
  std::cout << "cols: " << (int)imf.cols << std::endl;
  std::cout << "rows: " << (int)imf.rows << std::endl;
  std::cout << "planes: " << (int)imf.planes << std::endl;
  std::cout << "pixel: " << (int)imf.pixel_format << std::endl;
  std::cout << "channel: " << (int)imf.channel_type << std::endl;
}

// Correlator View
template <class FilterT>
inline stereo::CorrelatorView<PixelGray<float>,vw::uint8,FilterT>
correlator_helper( DiskImageView<PixelGray<float> > & left_disk_image,
                   DiskImageView<PixelGray<float> > & right_disk_image,
                   DiskImageView<vw::uint8> & left_mask,
                   DiskImageView<vw::uint8> & right_mask,
                   FilterT const& filter_func, vw::BBox2i & search_range,
                   stereo::CorrelatorType const& cost_mode,
                   bool draft_mode,
                   std::string & corr_debug_prefix,
                   bool use_pyramid=true ) {
  stereo::CorrelatorView<PixelGray<float>,
    vw::uint8,FilterT> corr_view( left_disk_image, right_disk_image,
                                  left_mask, right_mask, filter_func,
                                  use_pyramid );

  corr_view.set_search_range(search_range);
  corr_view.set_kernel_size(Vector2i(stereo_settings().h_kern,
                                     stereo_settings().v_kern));
  corr_view.set_cross_corr_threshold(stereo_settings().xcorr_threshold);
  corr_view.set_corr_score_threshold(stereo_settings().corrscore_rejection_threshold);
  corr_view.set_correlator_options(stereo_settings().cost_blur, cost_mode);

  if (draft_mode)
    corr_view.set_debug_mode(corr_debug_prefix);

  vw_out() << corr_view;
  vw_out() << "\t--> Building Disparity map." << std::endl;

  return corr_view;
}

//***********************************************************************
// MAIN
//***********************************************************************
int main(int argc, char* argv[]) {

  // The default file type are automatically registered the first time
  // a file is opened or created, however we want to override some of
  // the defaults, so we explicitly register them here before registering
  // our own FileIO driver code.
  DiskImageResource::register_default_file_types();

#if defined(ASP_HAVE_PKG_ISIS) && ASP_HAVE_PKG_ISIS == 1
  // Register the Isis file handler with the Vision Workbench
  // DiskImageResource system.
  DiskImageResource::register_file_type(".cub",
                                        DiskImageResourceIsis::type_static(),
                                        &DiskImageResourceIsis::construct_open,
                                        &DiskImageResourceIsis::construct_create);
#endif
  /*************************************/
  /* Parsing of command line arguments */
  /*************************************/

  // Boost has a nice command line parsing utility, which we use here
  // to specify the type, size, help string, etc, of the command line
  // arguments.
  int entry_point;
  unsigned num_threads;
  std::string stereo_session_string;
  std::string stereo_default_filename;
  std::string in_file1, in_file2, cam_file1, cam_file2, extra_arg1, extra_arg2, extra_arg3, extra_arg4;
  std::string out_prefix;
  std::string corr_debug_prefix;

  po::options_description visible_options("Options");
  visible_options.add_options()
    ("help,h", "Display this help message")
    ("threads", po::value<unsigned>(&num_threads)->default_value(0), "Select the number of processors (threads) to use.")
    ("session-type,t", po::value<std::string>(&stereo_session_string), "Select the stereo session type to use for processing. [options: pinhole isis]")
    ("stereo-file,s", po::value<std::string>(&stereo_default_filename)->default_value("./stereo.default"), "Explicitly specify the stereo.default file to use. [default: ./stereo.default]")
    ("entry-point,e", po::value<int>(&entry_point)->default_value(0), "Pipeline Entry Point (an integer from 1-4)")
    ("draft-mode", po::value<std::string>(&corr_debug_prefix)->default_value(""), "Cause the pyramid correlator to save out debug imagery named with this prefix.")
    ("optimized-correlator", "Use the optimized correlator instead of the pyramid correlator.");

  po::options_description positional_options("Positional Options");
  positional_options.add_options()
    ("left-input-image", po::value<std::string>(&in_file1), "Left Input Image")
    ("right-input-image", po::value<std::string>(&in_file2), "Right Input Image")
    ("left-camera-model", po::value<std::string>(&cam_file1), "Left Camera Model File")
    ("right-camera-model", po::value<std::string>(&cam_file2), "Right Camera Model File")
    ("output-prefix", po::value<std::string>(&out_prefix), "Prefix for output filenames")
    ("extra_argument1", po::value<std::string>(&extra_arg1), "Extra Argument 1")
    ("extra_argument2", po::value<std::string>(&extra_arg2), "Extra Argument 2")
    ("extra_argument3", po::value<std::string>(&extra_arg3), "Extra Argument 3")
    ("extra_argument4", po::value<std::string>(&extra_arg4), "Extra Argument 4");
  po::positional_options_description positional_options_desc;
  positional_options_desc.add("left-input-image", 1);
  positional_options_desc.add("right-input-image", 1);
  positional_options_desc.add("left-camera-model", 1);
  positional_options_desc.add("right-camera-model", 1);
  positional_options_desc.add("output-prefix", 1);
  positional_options_desc.add("extra_argument1", 1);
  positional_options_desc.add("extra_argument2", 1);
  positional_options_desc.add("extra_argument3", 1);
  positional_options_desc.add("extra_argument4", 1);

  po::options_description all_options;
  all_options.add(visible_options).add(positional_options);

  po::variables_map vm;
  po::store( po::command_line_parser( argc, argv ).options(all_options).positional(positional_options_desc).run(), vm );
  po::notify( vm );

  // If the command line wasn't properly formed or the user requested
  // help, we print an usage message.
  if( vm.count("help") ) {
    print_usage(visible_options);
    exit(0);
  }

  // Checking to see if the user has made a mistake in running the program
  if (!vm.count("left-input-image") || !vm.count("right-input-image") ||
      !vm.count("left-camera-model")) {
    vw_out() << std::endl << "Missing all of the correct input files."
              << std::endl;
    print_usage(visible_options);
    exit(0);
  }

  // Read the stereo.conf file
  stereo_settings().read(stereo_default_filename);

  // Set search range from stereo.default file
  BBox2i search_range(Vector2i(stereo_settings().h_corr_min, stereo_settings().v_corr_min),
                      Vector2i(stereo_settings().h_corr_max, stereo_settings().v_corr_max));

  // Set the Vision Workbench debug level
  if ( num_threads != 0 ) {
    vw_out() << "\t--> Setting number of processing threads to: "
              << num_threads << std::endl;
    vw_settings().set_default_num_threads(num_threads);
  }

  // Create a fresh stereo session and query it for the camera models.
  StereoSession::register_session_type( "rmax", &StereoSessionRmax::construct);
#if defined(ASP_HAVE_PKG_ISIS) && ASP_HAVE_PKG_ISIS == 1
  StereoSession::register_session_type( "isis", &StereoSessionIsis::construct);
#endif

  int MEDIAN_FILTER = 0;

  // If the user hasn't specified a stereo session type, we take a
  // guess here based on the file suffixes.
  if (stereo_session_string.size() == 0) {
    if ( (boost::iends_with(cam_file1, ".cahvor") && boost::iends_with(cam_file2, ".cahvor")) ||
         (boost::iends_with(cam_file1, ".cahv") && boost::iends_with(cam_file2, ".cahv")) ||
         (boost::iends_with(cam_file1, ".pin") && boost::iends_with(cam_file2, ".pin")) ||
         (boost::iends_with(cam_file1, ".tsai") && boost::iends_with(cam_file2, ".tsai")) ) {
         vw_out() << "\t--> Detected pinhole camera files.  Executing pinhole stereo pipeline.\n";
         stereo_session_string = "pinhole";
    }

    else if (boost::iends_with(in_file1, ".cub") && boost::iends_with(in_file2, ".cub")) {
      vw_out() << "\t--> Detected ISIS cube files.  Executing ISIS stereo pipeline.\n";
      stereo_session_string = "isis";
    }

    else {
      vw_out() << "\n\n******************************************************************\n";
      vw_out() << "Could not determine stereo session type.   Please set it explicitly\n";
      vw_out() << "using the -t switch.  Options include: [pinhole isis].\n";
      vw_out() << "******************************************************************\n\n";
      exit(0);
    }
  }

  // Some specialization here so that the user doesn't need to list
  // camera models on the command line for certain stereo session
  // types.  (e.g. isis).
  bool check_for_camera_models = true;
  if ( stereo_session_string == "isis" ) {
    // Fix the ordering of the arguments if the user only supplies 3
    if (out_prefix.size() == 0)
      out_prefix = cam_file1;
    check_for_camera_models = false;
  }

  if ( check_for_camera_models &&
       (!vm.count("output-prefix") || !vm.count("right-camera-model")) ) {
    vw_out() << "\nMissing output-prefix or right camera model." << std::endl;
    print_usage(visible_options);
    exit(0);
  }

  fs::path out_prefix_path(out_prefix);
  if (out_prefix_path.has_branch_path()) {
    if (!fs::is_directory(out_prefix_path.branch_path())) {
      vw_out() << "\nCreating output directory: " << out_prefix_path.branch_path() << std::endl;
      try {
        fs::create_directory(out_prefix_path.branch_path());
      } catch (fs::basic_filesystem_error<fs::path> & e) {
        vw_out() << "Error: " << e.what() << std::endl;
        exit(0);
      }
    }
  }

  StereoSession* session = StereoSession::create(stereo_session_string);
  session->initialize(in_file1, in_file2, cam_file1, cam_file2,
                      out_prefix, extra_arg1, extra_arg2, extra_arg3, extra_arg4);

  // The last thing we do before we get started is to copy the
  // stereo.default settings over into the results directory so that
  // we have a record of the most recent stereo.default that was used
  // with this data set.
  stereo_settings().copy_settings(stereo_default_filename, out_prefix + "-stereo.default");

  /*********************************************************************************/
  /*                            preprocessing step                                 */
  /*********************************************************************************/
  if (entry_point <= PREPROCESSING) {
    vw_out() << "\n[ " << current_posix_time_string() << " ] : Stage 0 --> PREPROCESSING \n";

    std::string pre_preprocess_file1, pre_preprocess_file2;
    session->pre_preprocessing_hook(in_file1, in_file2, pre_preprocess_file1, pre_preprocess_file2);

    DiskImageView<PixelGray<float> > left_rectified_image(pre_preprocess_file1);
    DiskImageView<PixelGray<float> > right_rectified_image(pre_preprocess_file2);
    ImageViewRef<PixelGray<float> > left_image = left_rectified_image;
    ImageViewRef<PixelGray<float> > right_image = right_rectified_image;

    if (MEDIAN_FILTER == 1){
      vw_out() << "\t--> Median filtering.\n" << std::flush;
      left_image  = fast_median_filter(left_rectified_image, 7);
      right_image = fast_median_filter(right_rectified_image, 7);
      write_image(out_prefix+"-median-L.tif", left_image,
                  TerminalProgressCallback( "asp", "\t--> Median L: "));
      write_image(out_prefix+"-median-R.tif", right_image,
                  TerminalProgressCallback( "asp", "\t--> Median R: "));
    }

    try {
      DiskImageView<PixelGray<uint8> >(out_prefix+"-lMask.tif");
      DiskImageView<PixelGray<uint8> >(out_prefix+"-rMask.tif");
      vw_out() << "\t--> Using cached image masks.\n";
    } catch (vw::Exception const& e) {
      vw_out() << "\t--> Generating image masks... \n";

      ImageViewRef<vw::uint8> Lmask = pixel_cast<vw::uint8>(threshold(apply_mask(edge_mask(left_image, 0, 0)),0,0,255));
      ImageViewRef<vw::uint8> Rmask = pixel_cast<vw::uint8>(threshold(apply_mask(edge_mask(right_image, 0, 0)),0,0,255));

      DiskImageResourceGDAL l_mask_rsrc( out_prefix+"-lMask.tif", Lmask.format(),
                                         Vector2i(vw_settings().default_tile_size(),
                                                  vw_settings().default_tile_size()) );
      DiskImageResourceGDAL r_mask_rsrc( out_prefix+"-rMask.tif", Rmask.format(),
                                         Vector2i(vw_settings().default_tile_size(),
                                                  vw_settings().default_tile_size()) );
      block_write_image(l_mask_rsrc, Lmask,
                        TerminalProgressCallback("asp", "\t    Mask L: "));
      block_write_image(r_mask_rsrc, Rmask,
                        TerminalProgressCallback("asp", "\t    Mask R: "));
    }

    // Produce subsampled images
    int smallest_edge = std::min(std::min(left_image.cols(), left_image.rows()),
                                 std::min(right_image.cols(), right_image.rows()) );
    float sub_scale = 2048.0 / float(smallest_edge);;
    if ( sub_scale > 1 ) sub_scale = 1;

    // Solving for the number of threads and the tile size to use for
    // subsampling while only using 500 MiB of memory. (The cache code
    // is a little slow on releasing so it will probably use 1.5GiB
    // memory during subsampling) Also tile size must be a power of 2
    // and greater than or equal to 64 px;
    int sub_threads = vw_settings().default_num_threads() + 1;
    int tile_power = 0;
    while ( tile_power < 6 && sub_threads > 1) {
      sub_threads--;
      tile_power = int( log10(500e6*sub_scale*sub_scale/(4.0*float(sub_threads)))/(2*log10(2)));
    }
    int sub_tile_size = int ( pow(2., tile_power) );
    if ( sub_tile_size > vw_settings().default_tile_size() )
      sub_tile_size = vw_settings().default_tile_size();

    std::string l_sub_file = out_prefix+"-L_sub.tif";
    std::string r_sub_file = out_prefix+"-R_sub.tif";

    try {
      DiskImageView<PixelGray<vw::float32> > Lsub(l_sub_file);
      DiskImageView<PixelGray<vw::float32> > Rsub(r_sub_file);
      vw_out() << "\t--> Using cached subsampled image.\n";
    } catch (vw::Exception const& e) {
      vw_out() << "\t--> Creating previews. Subsampling by " << sub_scale
               << " by using " << sub_tile_size << " tile size and "
               << sub_threads << " threads.\n";
      ImageViewRef<PixelGray<vw::float32> > Lsub = resample(left_image, sub_scale);
      ImageViewRef<PixelGray<vw::float32> > Rsub = resample(right_image, sub_scale);
      DiskImageResourceGDAL l_sub_rsrc( l_sub_file, Lsub.format(),
                                        Vector2i(sub_tile_size,
                                                 sub_tile_size) );
      DiskImageResourceGDAL r_sub_rsrc( r_sub_file, Rsub.format(),
                                        Vector2i(sub_tile_size,
                                                 sub_tile_size) );
      vw_settings().set_default_num_threads(sub_threads);
      block_write_image(l_sub_rsrc, Lsub,
                        TerminalProgressCallback("asp", "\t    Sub L: "));
      block_write_image(r_sub_rsrc, Rsub,
                        TerminalProgressCallback("asp", "\t    Sub R: "));
      vw_settings().set_default_num_threads();
    }

    // Auto Search Range
    if (stereo_settings().is_search_defined())
      vw_out() << "\t--> Using user defined search range: " << search_range << "\n";
    else
      search_range = approximate_search_range( l_sub_file, r_sub_file, sub_scale );
  }

  /*********************************************************************************/
  /*                       Discrete (integer) Correlation                          */
  /*********************************************************************************/
  if( entry_point <= CORRELATION ) {
    if (entry_point == CORRELATION)
        vw_out() << "\nStarting at the CORRELATION stage.\n";
    vw_out() << "\n[ " << current_posix_time_string() << " ] : Stage 1 --> CORRELATION \n";

    DiskImageView<vw::uint8> Lmask(out_prefix + "-lMask.tif");
    DiskImageView<vw::uint8> Rmask(out_prefix + "-rMask.tif");

    // Median filter
    std::string filename_L, filename_R;
    if (MEDIAN_FILTER==1){
      filename_L = out_prefix+"-median-L.tif";
      filename_R = out_prefix+"-median-R.tif";
    } else {
      filename_L = out_prefix+"-L.tif";
      filename_R = out_prefix+"-R.tif";
    }
    DiskImageView<PixelGray<float> > left_disk_image(filename_L);
    DiskImageView<PixelGray<float> > right_disk_image(filename_R);

    ImageViewRef<PixelMask<Vector2f> > disparity_map;
    stereo::CorrelatorType cost_mode = stereo::ABS_DIFF_CORRELATOR;
    if (stereo_settings().cost_mode == 1)
      cost_mode = stereo::SQR_DIFF_CORRELATOR;
    else if (stereo_settings().cost_mode == 2)
      cost_mode = stereo::NORM_XCORR_CORRELATOR;

    if (vm.count("optimized-correlator")) {
      if (stereo_settings().pre_filter_mode == 3) {
        vw_out() << "\t--> Using LOG pre-processing filter with width: "
                 << stereo_settings().slogW << std::endl;
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::SlogStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, false, corr_debug_prefix, false );
      } else if (stereo_settings().pre_filter_mode == 2) {
        vw_out() << "\t--> Using LOG pre-processing filter with width: "
                 << stereo_settings().slogW << std::endl;
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::LogStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, false, corr_debug_prefix, false );
      } else if (stereo_settings().pre_filter_mode == 1) {
        vw_out() << "\t--> Using BLUR pre-processing filter with width: "
                 << stereo_settings().slogW << std::endl;
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::BlurStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, false, corr_debug_prefix, false );
      } else {
        vw_out() << "\t--> Using NO pre-processing filter: " << std::endl;
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::NullStereoPreprocessingFilter(),
                                           search_range, cost_mode, false, corr_debug_prefix, false );
      }

    } else {
      bool draft_mode = vm.count("draft-mode");
      if (stereo_settings().pre_filter_mode == 3) {
        vw_out() << "\t--> Using SLOG pre-processing filter with " << stereo_settings().slogW << " sigma blur.\n";
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::SlogStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, draft_mode, corr_debug_prefix );
      } else if ( stereo_settings().pre_filter_mode == 2 ) {
        vw_out() << "\t--> Using LOG pre-processing filter with " << stereo_settings().slogW << " sigma blur.\n";
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::LogStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, draft_mode, corr_debug_prefix );
      } else if ( stereo_settings().pre_filter_mode == 1 ) {
        vw_out() << "\t--> Using BLUR pre-processing filter with " << stereo_settings().slogW << " sigma blur.\n";
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::BlurStereoPreprocessingFilter(stereo_settings().slogW),
                                           search_range, cost_mode, draft_mode, corr_debug_prefix );
      } else {
        vw_out() << "\t--> Using NO pre-processing filter." << std::endl;
        disparity_map = correlator_helper( left_disk_image, right_disk_image, Lmask, Rmask,
                                           stereo::NullStereoPreprocessingFilter(),
                                           search_range, cost_mode, draft_mode, corr_debug_prefix );
      }
    }

    // Create a disk image resource and prepare to write a tiled
    // OpenEXR.
    DiskImageResourceOpenEXR disparity_map_rsrc(out_prefix + "-D.exr", disparity_map.format() );
    disparity_map_rsrc.set_tiled_write(std::min(vw_settings().default_tile_size(),disparity_map.cols()),
                                       std::min(vw_settings().default_tile_size(),disparity_map.rows()), true);
    block_write_image( disparity_map_rsrc, disparity_map );
  }

  /*********************************************************************************/
  /*                            Subpixel Refinement                                */
  /*********************************************************************************/
  if( entry_point <= REFINEMENT ) {
    if (entry_point == REFINEMENT)
      vw_out() << "\nStarting at the REFINEMENT stage.\n";
    vw_out() << "\n[ " << current_posix_time_string() << " ] : Stage 2 --> REFINEMENT \n";

    try {
      std::string filename_L, filename_R;
      if (MEDIAN_FILTER == 1){
        filename_L = out_prefix+"-median-L.tif";
        filename_R = out_prefix+"-median-R.tif";
      } else {
        filename_L = out_prefix+"-L.tif";
        filename_R = out_prefix+"-R.tif";
      }
      DiskImageView<PixelGray<float> > left_disk_image(filename_L);
      DiskImageView<PixelGray<float> > right_disk_image(filename_R);
      DiskImageView<PixelMask<Vector2f> > disparity_disk_image(out_prefix + "-D.exr");
      ImageViewRef<PixelMask<Vector2f> > disparity_map = disparity_disk_image;

      if (stereo_settings().subpixel_mode == 0) {
        // Do nothing
      } else if (stereo_settings().subpixel_mode == 1) {
        // Parabola
        vw_out() << "\t--> Using parabola subpixel mode.\n";
        if (stereo_settings().pre_filter_mode == 3) {
          vw_out() << "\t    SLOG preprocessing width: "
                   << stereo_settings().slogW << "\n";
          typedef stereo::SlogStereoPreprocessingFilter PreFilter;
          disparity_map = stereo::SubpixelView<PreFilter>(disparity_disk_image,
                                                          channels_to_planes(left_disk_image),
                                                          channels_to_planes(right_disk_image),
                                                          stereo_settings().subpixel_h_kern,
                                                          stereo_settings().subpixel_v_kern,
                                                          stereo_settings().do_h_subpixel,
                                                          stereo_settings().do_v_subpixel,
                                                          stereo_settings().subpixel_mode,
                                                          PreFilter(stereo_settings().slogW),
                                                          false);
        } else if (stereo_settings().pre_filter_mode == 2) {
          vw_out() << "\t    LOG preprocessing width: "
                   << stereo_settings().slogW << "\n";
          typedef stereo::LogStereoPreprocessingFilter PreFilter;
          disparity_map = stereo::SubpixelView<PreFilter>(disparity_disk_image,
                                                          channels_to_planes(left_disk_image),
                                                          channels_to_planes(right_disk_image),
                                                          stereo_settings().subpixel_h_kern,
                                                          stereo_settings().subpixel_v_kern,
                                                          stereo_settings().do_h_subpixel,
                                                          stereo_settings().do_v_subpixel,
                                                          stereo_settings().subpixel_mode,
                                                          PreFilter(stereo_settings().slogW),
                                                          false);

        } else if (stereo_settings().pre_filter_mode == 1) {
          vw_out() << "\t    BLUR preprocessing width: "
                   << stereo_settings().slogW << "\n";
          typedef stereo::BlurStereoPreprocessingFilter PreFilter;
          disparity_map = stereo::SubpixelView<PreFilter>(disparity_disk_image,
                                                          channels_to_planes(left_disk_image),
                                                          channels_to_planes(right_disk_image),
                                                          stereo_settings().subpixel_h_kern,
                                                          stereo_settings().subpixel_v_kern,
                                                          stereo_settings().do_h_subpixel,
                                                          stereo_settings().do_v_subpixel,
                                                          stereo_settings().subpixel_mode,
                                                          PreFilter(stereo_settings().slogW),
                                                          false);
        } else {
          vw_out() << "\t    NO preprocessing" << std::endl;
          typedef stereo::NullStereoPreprocessingFilter PreFilter;
          disparity_map = stereo::SubpixelView<PreFilter>(disparity_disk_image,
                                                          channels_to_planes(left_disk_image),
                                                          channels_to_planes(right_disk_image),
                                                          stereo_settings().subpixel_h_kern,
                                                          stereo_settings().subpixel_v_kern,
                                                          stereo_settings().do_h_subpixel,
                                                          stereo_settings().do_v_subpixel,
                                                          stereo_settings().subpixel_mode,
                                                          PreFilter(),
                                                          false);
        }
      } else if (stereo_settings().subpixel_mode == 2) {
        // Bayes EM
        vw_out() << "\t--> Using affine adaptive subpixel mode "
                 << stereo_settings().subpixel_mode << "\n";
        vw_out() << "\t--> Forcing use of LOG filter with "
                 << stereo_settings().slogW << " sigma blur.\n";
        typedef stereo::LogStereoPreprocessingFilter PreFilter;
        disparity_map = stereo::SubpixelView<PreFilter>(disparity_disk_image,
                                                        channels_to_planes(left_disk_image),
                                                        channels_to_planes(right_disk_image),
                                                        stereo_settings().subpixel_h_kern,
                                                        stereo_settings().subpixel_v_kern,
                                                        stereo_settings().do_h_subpixel,
                                                        stereo_settings().do_v_subpixel,
                                                        stereo_settings().subpixel_mode,
                                                        PreFilter(stereo_settings().slogW),
                                                        false);
      } else if (stereo_settings().subpixel_mode == 3) {
        // Affine and Bayes subpixel refinement always use the
        // LogPreprocessingFilter...
        vw_out() << "\t--> Using EM Subpixel mode " << stereo_settings().subpixel_mode << std::endl;
        vw_out() << "\t--> Mode 3 does internal preprocessing; settings will be ignored. " << std::endl;

        stereo::EMSubpixelCorrelatorView<float32> em_correlator(channels_to_planes(left_disk_image),
                                                                channels_to_planes(right_disk_image),
                                                                disparity_disk_image, -1);
        em_correlator.set_em_iter_max(stereo_settings().subpixel_em_iter);
        em_correlator.set_inner_iter_max(stereo_settings().subpixel_affine_iter);
        em_correlator.set_kernel_size(Vector2i(stereo_settings().subpixel_h_kern,
                                               stereo_settings().subpixel_v_kern));
        em_correlator.set_pyramid_levels(stereo_settings().subpixel_pyramid_levels);
        //em_correlator.set_debug_region(BBox2i(25,25,1,1));

        //ImageView<PixelMask<Vector<float, 2> > > test_view(400, 400);

        DiskImageResourceOpenEXR em_disparity_map_rsrc(out_prefix + "-F6.exr", em_correlator.format());

        block_write_image(em_disparity_map_rsrc, em_correlator,
                          TerminalProgressCallback("asp", "\t--> EM Refinement :"));

        DiskImageResource *em_disparity_map_rsrc_2 = DiskImageResourceOpenEXR::construct_open(out_prefix + "-F6.exr");
        DiskImageView<PixelMask<Vector<float, 5> > > em_disparity_disk_image(em_disparity_map_rsrc_2);

        ImageViewRef<Vector<float, 3> > disparity_uncertainty = per_pixel_filter(em_disparity_disk_image,
                                                                                 stereo::EMSubpixelCorrelatorView<float32>::ExtractUncertaintyFunctor());
        ImageViewRef<float> spectral_uncertainty = per_pixel_filter(disparity_uncertainty,
                                                                    stereo::EMSubpixelCorrelatorView<float32>::SpectralRadiusUncertaintyFunctor());
        write_image(out_prefix+"-US.tif", spectral_uncertainty);
        write_image(out_prefix+"-U.tif", disparity_uncertainty);

        disparity_map = per_pixel_filter(em_disparity_disk_image, stereo::EMSubpixelCorrelatorView<float32>::ExtractDisparityFunctor());
      } else {
        vw_out() << "\t--> Invalid Subpixel mode selection: " << stereo_settings().subpixel_mode << std::endl;
        vw_out() << "\t--> Doing nothing\n";
      }

      // Create a disk image resource and prepare to write a tiled
      // OpenEXR.
      DiskImageResourceOpenEXR disparity_map_rsrc2(out_prefix + "-R.exr", disparity_map.format() );
      disparity_map_rsrc2.set_tiled_write(std::min(vw_settings().default_tile_size(),disparity_map.cols()),
                                          std::min(vw_settings().default_tile_size(), disparity_map.rows()));
      block_write_image( disparity_map_rsrc2, disparity_map,
                         TerminalProgressCallback("asp", "\t--> Refinement :") );

    } catch (IOErr &e) {
      vw_out(ErrorMessage) << "\nUnable to start at refinement stage -- could not read input files.\n" << e.what() << "\nExiting.\n\n";
      exit(0);
    }
  }

  /***************************************************************************/
  /*                      Disparity Map Filtering                            */
  /***************************************************************************/
  if(entry_point <= FILTERING) {
    if (entry_point == FILTERING)
      vw_out() << "\nStarting at the FILTERING stage.\n";
    vw_out() << "\n[ " << current_posix_time_string() << " ] : Stage 3 --> FILTERING \n";


    std::string post_correlation_fname;
    session->pre_filtering_hook(out_prefix+"-R.exr", post_correlation_fname);

    try {

      // Rasterize the results so far to a temporary file on disk.
      // This file is deleted once we complete the second half of the
      // disparity map filtering process.
      {
        // Apply filtering for high frequencies
        DiskImageView<PixelMask<Vector2f> > disparity_disk_image(post_correlation_fname);
        ImageViewRef<PixelMask<Vector2f> > disparity_map = disparity_disk_image;

        vw_out() << "\t--> Cleaning up disparity map prior to filtering processes (" << stereo_settings().rm_cleanup_passes << " passes).\n";
        for (int i = 0; i < stereo_settings().rm_cleanup_passes; ++i)
          disparity_map = stereo::disparity_clean_up(disparity_map,
                                                     stereo_settings().rm_h_half_kern,
                                                     stereo_settings().rm_v_half_kern,
                                                     stereo_settings().rm_threshold,
                                                     stereo_settings().rm_min_matches/100.0);

        // Applying additional clipping from the edge. We make new
        // mask files to avoid a weird and tricky segfault due to
        // ownership issues.
        {
          DiskImageView<vw::uint8> left_mask( out_prefix+"-lMask.tif" );
          DiskImageView<vw::uint8> right_mask( out_prefix+"-rMask.tif" );
          int mask_buffer = std::max( stereo_settings().subpixel_h_kern,
                                      stereo_settings().subpixel_v_kern );
          ImageViewRef<vw::uint8> Lmaskmore, Rmaskmore;
          Lmaskmore = apply_mask(edge_mask(left_mask,0,mask_buffer));
          Rmaskmore = apply_mask(edge_mask(right_mask,0,mask_buffer));
          DiskImageResourceGDAL l_mask_rsrc( out_prefix+"-lMaskMore.tif", Lmaskmore.format(),
                                         Vector2i(vw_settings().default_tile_size(),
                                                  vw_settings().default_tile_size()) );
          DiskImageResourceGDAL r_mask_rsrc( out_prefix+"-rMaskMore.tif", Rmaskmore.format(),
                                             Vector2i(vw_settings().default_tile_size(),
                                                      vw_settings().default_tile_size()) );
          block_write_image(l_mask_rsrc, Lmaskmore,
                            TerminalProgressCallback("asp", "\t   Reduce LMask: "));
          block_write_image(r_mask_rsrc, Rmaskmore,
                            TerminalProgressCallback("asp", "\t   Reduce RMask: "));
        }
        DiskImageView<vw::uint8> left_mask( out_prefix+"-lMaskMore.tif" );
        DiskImageView<vw::uint8> right_mask( out_prefix+"-rMaskMore.tif" );

        disparity_map = stereo::disparity_mask( disparity_map,
                                                left_mask, right_mask );

        vw_out() << "\t--> Rasterizing filtered disparity map to disk. \n";
        DiskImageResourceOpenEXR filtered_disparity_map_rsrc( out_prefix+"-FTemp.exr", disparity_map.format() );
        filtered_disparity_map_rsrc.set_block_size( Vector2i( vw_settings().default_tile_size(),
                                                              vw_settings().default_tile_size() ) );
        block_write_image( filtered_disparity_map_rsrc, disparity_map,
                           TerminalProgressCallback("asp", "\t--> Writing: ") );

        // Removing extra masks
        std::string lmask = out_prefix+"-lMaskMore.tif",
          rmask = out_prefix+"-rMaskMore.tif";
        unlink( lmask.c_str() );
        unlink( rmask.c_str() );
      }
      DiskImageView<PixelMask<Vector2f> > filtered_disparity_map( out_prefix+"-FTemp.exr" );

      {
        vw_out() << "\t--> Creating \"Good Pixel\" image: "
                  << (out_prefix + "-GoodPixelMap.tif") << "\n";
        {
          // Sub-sampling so that the user can actually view it.
          float sub_scale = 2048 / float( std::min( filtered_disparity_map.cols(),
                                                    filtered_disparity_map.rows() ) );
          if ( sub_scale > 1 ) sub_scale = 1;
          // Solving for the number of threads and the tile size to use for
          // subsampling while only using 500 MiB of memory. (The cache code
          // is a little slow on releasing so it will probably use 1.5GiB
          // memory during subsampling) Also tile size must be a power of 2
          // and greater than or equal to 64 px;
          int sub_threads = vw_settings().default_num_threads() + 1;
          int tile_power = 0;
          while ( tile_power < 6 && sub_threads > 1) {
            sub_threads--;
            tile_power = int( log10(500e6*sub_scale*sub_scale/(4.0*float(sub_threads)))/(2*log10(2)));
          }
          int sub_tile_size = int ( pow(2., tile_power) );
          if ( sub_tile_size > vw_settings().default_tile_size() )
            sub_tile_size = vw_settings().default_tile_size();

          ImageViewRef<PixelRGB<uint8> > good_pixel = resample(stereo::missing_pixel_image(filtered_disparity_map),
                                                               sub_scale);
          vw_settings().set_default_num_threads(sub_threads);
          DiskImageResourceGDAL good_pixel_rsrc( out_prefix + "-GoodPixelMap.tif", good_pixel.format(),
                                                 Vector2i(sub_tile_size,
                                                          sub_tile_size ) );
          block_write_image( good_pixel_rsrc, good_pixel,
                             TerminalProgressCallback("asp", "\t    Writing: "));
          vw_settings().set_default_num_threads(sub_threads);
        }
      }

      ImageViewRef<PixelMask<Vector2f> > hole_filled_disp_map;
      if(stereo_settings().fill_holes) {
        vw_out() << "\t--> Filling holes with Inpainting method.\n";
        BlobIndexThreaded bindex( invert_mask( filtered_disparity_map ), stereo_settings().fill_hole_max_size );
        vw_out() << "\t    * Identified " << bindex.num_blobs() << " holes\n";
        hole_filled_disp_map = InpaintView<DiskImageView<PixelMask<Vector2f> > >(filtered_disparity_map, bindex );
      } else {
        hole_filled_disp_map = filtered_disparity_map;
      }

      DiskImageResourceOpenEXR disparity_map_rsrc(out_prefix + "-F.exr", hole_filled_disp_map.format() );
      disparity_map_rsrc.set_tiled_write(std::min(vw_settings().default_tile_size(),
                                                  hole_filled_disp_map.cols()),
                                         std::min(vw_settings().default_tile_size(),
                                                  hole_filled_disp_map.rows()));

      block_write_image(disparity_map_rsrc, hole_filled_disp_map,
                        TerminalProgressCallback("asp", "\t--> Filtering: ") );

      // Delete temporary file
      std::string temp_file =  out_prefix+"-FTemp.exr";
      unlink( temp_file.c_str() );
    } catch (IOErr &e) {
      vw_out() << "\nUnable to start at filtering stage -- could not read input files.\n"
                << e.what() << "\nExiting.\n\n";
      exit(0);
    }
  }


  /******************************************************************************/
  /*                               TRIANGULATION                                */
  /******************************************************************************/
  if (entry_point <= POINT_CLOUD) {
    if (entry_point == POINT_CLOUD)
      vw_out() << "\nStarting at the TRIANGULATION stage.\n";
    vw_out() << "\n[ " << current_posix_time_string() << " ] : Stage 4 --> TRIANGULATION \n";

    try {
      boost::shared_ptr<camera::CameraModel> camera_model1, camera_model2;
      session->camera_models(camera_model1, camera_model2);

      // If the user has generated a set of position and pose
      // corrections using the bundle_adjust program, we read them in
      // here and incorporate them into our camera model.
      Vector3 position_correction;
      Quaternion<double> pose_correction;
      if (fs::exists(prefix_from_filename(in_file1)+".adjust")) {
        read_adjustments(prefix_from_filename(in_file1)+".adjust", position_correction, pose_correction);
        camera_model1 = boost::shared_ptr<CameraModel>(new AdjustedCameraModel(camera_model1,
                                                                               position_correction,
                                                                               pose_correction));
      }
      if (fs::exists(prefix_from_filename(in_file2)+".adjust")) {
        read_adjustments(prefix_from_filename(in_file2)+".adjust", position_correction, pose_correction);
        camera_model2 = boost::shared_ptr<CameraModel>(new AdjustedCameraModel(camera_model2,
                                                                               position_correction,
                                                                               pose_correction));
      }

      std::string prehook_filename;
      session->pre_pointcloud_hook(out_prefix+"-F.exr", prehook_filename);
      DiskImageView<PixelMask<Vector2f> > disparity_map(prehook_filename);

      // Apply the stereo model.  This yields a image of 3D points in
      // space.  We build this image and immediately write out the
      // results to disk.
      vw_out() << "\t--> Generating a 3D point cloud.   " << std::endl;
      stereo::StereoView<DiskImageView<PixelMask<Vector2f> > > stereo_image(disparity_map,
                                                                            camera_model1.get() ,
                                                                            camera_model2.get() );

      // If the distance from the left camera center to a point is
      // greater than the universe radius, we remove that pixel and
      // replace it with a zero vector, which is the missing pixel value
      // in the point_image.
      //
      // We apply the universe radius here and then write the result
      // directly to a file on disk.
      stereo::UniverseRadiusFunc universe_radius_func(camera_model1->camera_center(Vector2(0,0)),
                                                      stereo_settings().near_universe_radius,
                                                      stereo_settings().far_universe_radius);

      ImageViewRef<Vector3> point_cloud = per_pixel_filter(stereo_image, universe_radius_func);


      DiskImageResourceGDAL point_cloud_rsrc(out_prefix + "-PC.tif", point_cloud.format(),
                                             Vector2i(vw_settings().default_tile_size(),
                                                      vw_settings().default_tile_size()) );

      if ( stereo_session_string == "isis" ) {
        write_image(point_cloud_rsrc, point_cloud,
                    TerminalProgressCallback("asp", "\t--> Triangulating: "));
      } else
        block_write_image(point_cloud_rsrc, point_cloud, TerminalProgressCallback(InfoMessage, "\t--> Triangulating: "));
      vw_out() << "\t--> " << universe_radius_func;
    } catch (IOErr &e) {
      vw_out() << "\nUnable to start at point cloud stage -- could not read input files.\n"
               << e.what() << "\nExiting.\n\n";
      exit(0);
    }

  }

  vw_out() << "\n[ " << current_posix_time_string() << " ] : FINISHED \n";

  return(EXIT_SUCCESS);
}
