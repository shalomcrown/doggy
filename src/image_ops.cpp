#include "image_ops.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/ximgproc.hpp>

// ================================================================================

cv::Mat despeckle_disparity(
        const cv::Mat &disparity,
        int max_speckle_diff,
        int max_speckle_size) {
    cv::Mat cleaned = disparity.clone();
    if (cleaned.empty()) {
        return cleaned;
    }
    cv::filterSpeckles(cleaned, 0, max_speckle_size, max_speckle_diff);
    return cleaned;
}

// ================================================================================

cv::Mat median_smooth(const cv::Mat &image, int ksize) {
    cv::Mat smoothed;
    if (image.empty() || ksize < 3 || (ksize % 2) == 0) {
        return image.clone();
    }
    cv::medianBlur(image, smoothed, ksize);
    return smoothed;
}

// ================================================================================

cv::Mat denoise_gray(const cv::Mat &gray) {
    cv::Mat denoised;
    if (gray.empty() || gray.type() != CV_8UC1) {
        return gray.clone();
    }
    cv::fastNlMeansDenoising(gray, denoised, 3, 7, 21);
    return denoised;
}

// ================================================================================

cv::Mat stereo_disparity(const cv::Mat &left_gray, const cv::Mat &right_gray) {
    if (left_gray.empty() || right_gray.size() != left_gray.size()
            || left_gray.type() != CV_8UC1 || right_gray.type() != CV_8UC1) {
        return {};
    }

    const cv::Ptr<cv::StereoSGBM> matcher = cv::StereoSGBM::create(0, 64, 5);
    matcher->setP1(8 * 5 * 5);
    matcher->setP2(32 * 5 * 5);
    matcher->setUniquenessRatio(10);
    matcher->setSpeckleWindowSize(50);
    matcher->setSpeckleRange(2);
    matcher->setDisp12MaxDiff(1);
    cv::Mat left_disparity;
    matcher->compute(left_gray, right_gray, left_disparity);
    const cv::Ptr<cv::StereoMatcher> right_matcher =
            cv::ximgproc::createRightMatcher(matcher);
    cv::Mat right_disparity;
    right_matcher->compute(right_gray, left_gray, right_disparity);
    const cv::Ptr<cv::ximgproc::DisparityWLSFilter> filter =
            cv::ximgproc::createDisparityWLSFilter(matcher);
    filter->setLambda(8000.0);
    filter->setSigmaColor(1.5);
    cv::Mat smoothed;
    filter->filter(left_disparity, left_gray, smoothed, right_disparity);
    return despeckle_disparity(smoothed, 16, 50);
}
