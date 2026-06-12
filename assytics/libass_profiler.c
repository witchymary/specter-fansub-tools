#include <stdio.h>
#include <time.h>
#include <getopt.h>
#include <ass/ass.h>

float fps = 23.976;

typedef struct {
    char *inputfilename;
    char *outputfilename;
    int   res_width;
    int   res_height;
} config_t;

void printhelp(char *defaultoutputfile) {
  printf("Usage: libass_profiler [options] ASSFILE\n");
  printf("Options:\n");
  printf("  --help,       -h               Print this message and exit.\n");
  printf("  --output,     -o PATH          Output CSV file (default: %s).\n", defaultoutputfile);
  printf("  --resolution, -r WIDTHxHEIGHT  Canvas resolution for the ASS renderer (default: from ASSFILE).\n");
}

int timecode_string(char* output, long long ms_long){
  int cs = (ms_long % 1000) / 10;
  int s = (ms_long % 60000) / 1000;
  int m = (ms_long % 3600000) / 60000;
  int h = ms_long / 3600000;
  return sprintf(output,"%01d:%02d:%02d.%02d",h,m,s,cs);
}

long long find_track_duration_in_ms(ASS_Track* track){
  long long retval = 0;
  for (int i =0; i < track->n_events; i++){
    ASS_Event event = track->events[i];
    long long event_endtime = event.Start + event.Duration;
    retval = retval > event_endtime ? retval : event_endtime;
  }
  return retval;
}

int parse_args(int argc, char *argv[], config_t *cfg) {
  int opt;

  const char *short_opts = ":ho:r:";
  static struct option long_opts[] =
    {
      {"help",       no_argument,       NULL,  'h'},
      {"output",     required_argument, NULL,  'o'},
      {"resolution", required_argument, NULL,  'r'},
      {NULL,         0,                 NULL,    0}
    };

  while (1) {
    opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
    if (opt == -1) break;

    switch(opt) {
      case 'h':
        printhelp(cfg->outputfilename);
        return 1;

      case 'o':
        cfg->outputfilename = optarg;
        break;

      case 'r':
        if (sscanf(optarg, "%dx%d", &cfg->res_width, &cfg->res_height) != 2) {
          printf("Invalid resolution format '%s'. Expected WIDTHxHEIGHT (e.g., 1920x1080).\n", optarg);
          return 1;
        }
        if (cfg->res_width <= 0 || cfg->res_height <= 0) {
          printf("Resolution dimensions must be positive integers (got %dx%d).\n", cfg->res_width, cfg->res_height);
          return 1;
        }
        break;

      case '?':
        // This is needed otherwise clustered options (e.g., -xo) would not be printed properly
        if (optopt) {
          printf("Unknown option: -%c\n", optopt);
        } else {
          printf("Unknown option: %s\n", argv[optind - 1]);
        }
        return 1;

      case ':':
        printf("Missing argument for %s\n", argv[optind - 1]);
        return 1;

      default:
        printf("Unexpected getopt_long return value: %c\n", (char)opt);
        return 1;
    }
  }

  int nargs = argc - optind;
  if (argc == 1) {
    printhelp(cfg->outputfilename);
    return 1;
  }

  if (nargs != 1) {
    printf("Expected one positional argument, got %d\n", nargs);
    return 1;
  }

  cfg->inputfilename = argv[optind];
  return 0;
}

int main(int argc, char *argv[]) {
  config_t cfg = {
    .outputfilename = "output.csv",
    .res_width      = 0,
    .res_height     = 0,
    .inputfilename  = NULL,
  };

  if (parse_args(argc, argv, &cfg) != 0) return 1;

  int version = ass_library_version();
  printf("libass version %d\n",version);

  ASS_Library* my_ass_library = ass_library_init();
  ass_set_extract_fonts(my_ass_library,1);
  ASS_Track* my_ass_track = ass_read_file(my_ass_library,cfg.inputfilename,NULL);
  ASS_Renderer* my_ass_renderer = ass_renderer_init(my_ass_library);

  if (!cfg.res_width && !cfg.res_height) {
    cfg.res_width  = my_ass_track->PlayResX;
    cfg.res_height = my_ass_track->PlayResY;
  }

  if (cfg.res_width <= 0 || cfg.res_height <= 0) {
    printf("Canvas resolution is %dx%d; ASSFILE may be missing PlayResX/PlayResY headers.\n",
        cfg.res_width, cfg.res_height);
    return 1;
  }

  ass_set_frame_size(my_ass_renderer, cfg.res_width, cfg.res_height);
  ass_set_fonts(my_ass_renderer,NULL,"Sans",1,NULL,1);
  long long track_duration = find_track_duration_in_ms(my_ass_track);

  FILE* outfile = fopen(cfg.outputfilename,"w");
  fprintf(outfile, "%s\n", cfg.inputfilename);
  fprintf(outfile, "time,total_image_size,largest_image_size,image_count,time_benchmark\n");
  for (long long t = 0; t < track_duration; t = t + 1000/fps) {
    long long frame_total_image_size = 0;
    long long frame_largest_image_size = 0;
    long long frame_image_count = 0;
    clock_t begin = clock();
    ASS_Image* my_ass_images = ass_render_frame(my_ass_renderer,my_ass_track,t,NULL);
    double frame_time_benchmark = (double)(clock() - begin) / CLOCKS_PER_SEC;
    while(my_ass_images != NULL){
      int h = my_ass_images->h;
      int w = my_ass_images->w;
      int stride = my_ass_images->stride;
      long long image_size = stride*(h-1) + w;
      frame_total_image_size += image_size;
      frame_largest_image_size = frame_largest_image_size > image_size ? frame_largest_image_size : image_size;
      frame_image_count++;
      my_ass_images = my_ass_images->next;
    }
    //write to stats
    char timecode[40];
    timecode_string(timecode, t);
    fprintf(outfile, "%s,%lld,%lld,%lld,%lf\n", timecode,frame_total_image_size,frame_largest_image_size,frame_image_count,frame_time_benchmark);
  }
  fclose(outfile);
  return 0;
}
