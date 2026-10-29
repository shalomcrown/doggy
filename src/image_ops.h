#ifndef IMAGE_OPS_H
#define IMAGE_OPS_H

#include <opencv2/core.hpp>

// ================================================================================

// Removes small disparity blobs. disparity is the 16-bit map StereoSGBM writes.
cv::Mat despeckle_disparity(
        const cv::Mat &disparity,
        int max_speckle_diff,
        int max_speckle_size);

// ================================================================================

cv::Mat median_smooth(const cv::Mat &image, int ksize);

// ================================================================================

cv::Mat denoise_gray(const cv::Mat &gray);

// ================================================================================

// StereoSGBM disparity, then the weighted least-squares smoother from ximgproc.
cv::Mat stereo_disparity(const cv::Mat &left_gray, const cv::Mat &right_gray);

#endif
