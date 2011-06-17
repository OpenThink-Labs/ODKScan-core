#include "formAlignment.h"
#include "highgui.h"

#include "nameGenerator.h"
NameGenerator alignmentNamer("debug_segment_images/");

//Image straightening constants
#define DILATION 6
#define BLOCK_SIZE 3
#define DIST_PARAM 500

//image_align constants
#define THRESH_OFFSET_LB -.3
#define THRESH_DECR_SIZE .05

#define DEBUG_ALIGN_IMAGE 1

using namespace cv;

//Takes a vector of found corners, and an array of destination corners they should map to
//and replaces each corner in the dest_corners with the nearest unmatched found corner.
//The optional expand arguement moves each found corner in the direction of the destination
//corner it is mapped to.
template <class Tp>
void configCornerArray(vector<Tp>& found_corners, Point2f* dest_corners, float expand = 0) {
	float min_dist;
	int min_idx;
	float dist;

	vector<Point2f> corners;

	for(int i = 0; i < found_corners.size(); i++ ){
		corners.push_back(Point2f(float(found_corners[i].x), float(found_corners[i].y)));
	}
	for(int i = 0; i < 4; i++) {
		min_dist = FLT_MAX;
		for(int j = 0; j < corners.size(); j++ ){
			dist = norm(corners[j]-dest_corners[i]);
			if(dist < min_dist){
				min_dist = dist;
				min_idx = j;
			}
		}
		dest_corners[i]=corners[min_idx] + expand * (dest_corners[i] - corners[min_idx]);
		corners.erase(corners.begin()+min_idx);
	}
}
//Finds two vertical or horizontal lines that have the minimal gradient sum.
void find_bounding_lines(Mat& img, int* upper, int* lower, bool vertical) {
	int center_size = 20 * SCALEPARAM;
	Mat grad_img, out;
	Sobel(img, grad_img, 0, int(!vertical), int(vertical));

	reduce(grad_img, out, int(!vertical), CV_REDUCE_SUM, CV_32F);
	GaussianBlur(out, out, Size(1,(int)(3 * SCALEPARAM)), 1.0 * SCALEPARAM);

	if( vertical )
		transpose(out,out);

	Point min_location_top;
	Point min_location_bottom;
	minMaxLoc(out(Range(3, out.rows/2 - center_size), Range(0,1)), NULL,NULL,&min_location_top);
	minMaxLoc(out(Range(out.rows/2 + center_size,out.rows - 3), Range(0,1)), NULL,NULL,&min_location_bottom);
	*upper = min_location_top.y;
	*lower = min_location_bottom.y + out.rows/2 + center_size;
}
//Thresholds the image using its mean pixel intensity.
//thresh_offset specified an offset in units of standard deviations.
void my_threshold(Mat& img, Mat& thresholded_img, float thresh_offset) {
	//The ideal image would be black lines and white boxes with nothing in them
	//so if we can filter to get something closer to that, it is a good thing.
	//One of the big problems with thresholding is that it is thrown off by filled in bubbles.
	//Equalizing seems to mitigate this somewhat.
	//It might help use the same threshold level for all the segments, but if one is in shadow
	//it will cause problems.
	Mat equalized_img;
	equalizeHist(img, equalized_img);
	Scalar my_mean;
	Scalar my_stddev;
	meanStdDev(equalized_img, my_mean, my_stddev);
	//Everything before this point could be precompted in some cases.
	//If we ever need to speed things up that is something to consider,
	//however debuggging is easier this way. 
	thresholded_img = equalized_img > (my_mean.val[0] - thresh_offset * my_stddev.val[0]);
}
//Find the largest simplified contour
float findRectContour(Mat& img, vector<Point>& maxRect){
	vector < vector<Point> > contours;
	vector < Point > approx;
	// Need this to prevent find contours from destroying the image.
	Mat img2;
	img.copyTo(img2);
	// Find all external contours of the image
	findContours(img2, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);

	float maxContourArea = 0;
	// Iterate through all detected contours
	for (size_t i = 0; i < contours.size(); ++i) {
		// reduce the number of points in the contour
		approxPolyDP(Mat(contours[i]), approx,
				     arcLength(Mat(contours[i]), true) * 0.1, true);

		float area = fabs(contourArea(Mat(approx)));

		if (area > maxContourArea) {
			maxRect = approx;
			maxContourArea = area;
		}
	}
	return maxContourArea;
}
// This function take an image looks for a large rectangle.
// If it finds one it transforms that rectangle so that it fits into aligned_image.
// This can be used in place of straighten image, however it does not seem to
// produce as nice of results.
void align_image(Mat& img, Mat& aligned_image, float thresh_seed = 1.0){
	Mat imgThresh;
	vector<Point> maxRect;
	float maxContourArea;
	
	// Find an appropriately size rectangular section of an image
	// by iteratively decreasing the threshold offset (which means more black).
	while( true ){
		my_threshold(img, imgThresh, thresh_seed);
		maxContourArea = findRectContour(imgThresh, maxRect);
		thresh_seed -= THRESH_DECR_SIZE;
		if(thresh_seed < THRESH_OFFSET_LB)
			break;
		if( maxRect.size() == 4 && isContourConvex(Mat(maxRect)) && maxContourArea > (img.cols/2) * (img.rows/2))
			break;
	}
	
	#if DEBUG_ALIGN_IMAGE > 0
	Mat dbg_out;
	imgThresh.convertTo(dbg_out, CV_8U);
	string segfilename = alignmentNamer.get_unique_name("alignment_debug_");
	segfilename.append(".jpg");
	imwrite(segfilename, dbg_out);
	#endif
	
	if ( maxRect.size() == 4 && isContourConvex(Mat(maxRect)) && maxContourArea > (img.cols/2) * (img.rows/2)) {
		Point2f segment_corners[4] = {Point2f(0,0),Point2f(aligned_image.cols,0),
		Point2f(0,aligned_image.rows),Point2f(aligned_image.cols,aligned_image.rows)};
		Point2f corners_a[4] = {Point2f(0,0),Point2f(img.cols,0),
		Point2f(0,img.rows),Point2f(img.cols,img.rows)};

		configCornerArray(maxRect, corners_a, .1);
		Mat H = getPerspectiveTransform(corners_a , segment_corners);
		warpPerspective(img, aligned_image, H, aligned_image.size());
	}
	else{//use the bounding line method if the contour method fails
		int top = 0, bottom = 0, left = 0, right = 0;
		find_bounding_lines(img, &top, &bottom, false);
		find_bounding_lines(img, &left, &right, true);

		#if DEBUG_ALIGN_IMAGE > 0
		img.copyTo(dbg_out);
		const Point* p = &maxRect[0];
		int n = (int) maxRect.size();
		polylines(dbg_out, &p, &n, 1, true, 200, 2, CV_AA);

		dbg_out.row(top)+=200;
		dbg_out.row(bottom)+=200;
		dbg_out.col(left)+=200;
		dbg_out.col(right)+=200;
		string segfilename = alignmentNamer.get_unique_name("alignment_debug_");
		segfilename.append(".jpg");
		imwrite(segfilename, dbg_out);
		#endif
		
		float bounding_lines_threshold = .2;
		if ((abs((bottom - top) - aligned_image.rows) < bounding_lines_threshold * aligned_image.rows) &&
			(abs((right - left) - aligned_image.cols) < bounding_lines_threshold * aligned_image.cols) &&
			top + aligned_image.rows < img.rows &&  top + aligned_image.cols < img.cols) {

			img(Rect(left, top, aligned_image.cols, aligned_image.rows)).copyTo(aligned_image);

		}
		else{
			img(Rect(SEGMENT_BUFFER * SCALEPARAM, SEGMENT_BUFFER * SCALEPARAM,
				aligned_image.cols, aligned_image.rows)).copyTo(aligned_image);
		}
	}
}
//A form straitening method based on finding the corners of the sheet of form paper.
//The form will be resized to the size of output_image.
//It might save some memory to specify a Size object instead of a preallocated Mat.
void straightenImage(const Mat& input_image, Mat& output_image) {
	Point2f orig_corners[4];
	Point2f corners_a[4];
	vector < Point2f > corners;

	Mat tmask, input_image_dilated;

	// Create a mask that limits corner detection to the corners of the image.
	tmask= Mat::zeros(input_image.rows, input_image.cols, CV_8U);
	circle(tmask, Point(0,0), (tmask.cols+tmask.rows)/8, Scalar(255,255,255), -1);
	circle(tmask, Point(0,tmask.rows), (tmask.cols+tmask.rows)/8, Scalar(255,255,255), -1);
	circle(tmask, Point(tmask.cols,0), (tmask.cols+tmask.rows)/8, Scalar(255,255,255), -1);
	circle(tmask, Point(tmask.cols,tmask.rows), (tmask.cols+tmask.rows)/8, Scalar(255,255,255), -1);

	//orig_corners = {Point(0,0),Point(img.cols,0),Point(0,img.rows),Point(img.cols,img.rows)};
	orig_corners[0] = Point(0,0);
	orig_corners[1] = Point(output_image.cols,0);
	orig_corners[2] = Point(0,output_image.rows);
	orig_corners[3] = Point(output_image.cols,output_image.rows);

	// Dilating reduces noise, thin lines and small marks.
	dilate(input_image, input_image_dilated, Mat(), Point(-1, -1), DILATION);

	/*
	Params for goodFeaturesToTrack:
	Source Mat, Dest Mat
	Number of features/interest points to return
	Minimum feature quality
	Min distance between corners (Probably needs parameterization depending on im. res. and form)
	Mask
	Block Size (not totally sure but I think it's similar to aperture)
	Use Harris detector (true) or cornerMinEigenVal() (false)
	Free parameter of Harris detector
	*/
	goodFeaturesToTrack(input_image_dilated, corners, 4, 0.01, DIST_PARAM, tmask, BLOCK_SIZE, false, 0.04);

	// Initialize the value of corners_a to that of orig_corners
	memcpy(corners_a, orig_corners, sizeof(orig_corners));
	configCornerArray(corners, corners_a);

	Mat H = getPerspectiveTransform(corners_a , orig_corners);
	
	warpPerspective(input_image, output_image, H, output_image.size());

}
