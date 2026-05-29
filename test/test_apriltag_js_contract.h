#ifndef TEST_APRILTAG_JS_CONTRACT_H
#define TEST_APRILTAG_JS_CONTRACT_H

void when_browser_wrapper_uses_conservative_demo_detector_options(void **state);
void when_apriltag_js_exposes_set_tag_family_wrapper(void **state);
void when_camera_demo_exposes_aruco_family_configuration(void **state);
void when_user_leaves_demo_page_camera_tracks_are_stopped(void **state);
void when_user_leaves_demo_page_video_processing_stops(void **state);

#endif
