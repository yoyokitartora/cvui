/*
 A (very) simple UI lib built on top of OpenCV drawing primitives.

 Version: 1.0.0

 Copyright (c) 2016 Fernando Bevilacqua <dovyski@gmail.com>
 Licensed under the MIT license.
*/

#include <iostream>

#include "opencv2/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "cvui.h"

namespace cvui
{

// Variables to keep track of mouse events and stuff
static bool gMouseJustReleased = false;
static bool gMousePressed = false;
static cv::Point gMouse;
static char gBuffer[1024];
static cvui_block_t gScreen;

// This is an internal namespace with all code
// that is shared among components/functions
namespace internal {
	static cvui_block_t gStack[100]; // TODO: make it dynamic?
	static int gStackCount = -1;

	void error(int theId, std::string theMessage) {
		std::cout << "[CVUI] Fatal error (code " << theId << "): " << theMessage << "\n";
		cv::waitKey(100000);
		exit(-1);
	}

	void updateLayoutFlow(cvui_block_t& theBlock, cv::Size theSize) {
		int aValue;

		if (theBlock.type == ROW) {
			aValue = theSize.width + theBlock.padding;

			theBlock.anchor.x += aValue;
			theBlock.fill.width += aValue;
			theBlock.fill.height = std::max(theSize.height, theBlock.fill.height);

		}
		else if (theBlock.type == COLUMN) {
			aValue = theSize.height + theBlock.padding;

			theBlock.anchor.y += aValue;
			theBlock.fill.height += aValue;
			theBlock.fill.width = std::max(theSize.width, theBlock.fill.width);
		}
	}

	bool blockStackEmpty() {
		return gStackCount == -1;
	}

	cvui_block_t& topBlock() {
		if (gStackCount < 0) {
			error(3, "You are using a function that should be enclosed by begin*() and end*(), but you probably forgot to call begin*().");
		}

		return gStack[gStackCount];
	}

	cvui_block_t& pushBlock() {
		return gStack[++gStackCount];
	}

	cvui_block_t& popBlock() {
		// Check if there is anything to be popped out from the stack.
		if (gStackCount < 0) {
			error(1, "Mismatch in the number of begin*()/end*() calls. You are calling one more than the other.");
		}
		
		return gStack[gStackCount--];
	}

	void begin(int theType, cv::Mat &theWhere, int theX, int theY, int theWidth, int theHeight, int thePadding) {
		cvui_block_t& aBlock = internal::pushBlock();

		aBlock.where = theWhere;
		
		aBlock.rect.x = theX;
		aBlock.rect.y = theY;
		aBlock.rect.width = theWidth;
		aBlock.rect.height = theHeight;
		
		aBlock.fill = aBlock.rect;
		aBlock.fill.width = 0;
		aBlock.fill.height = 0;

		aBlock.anchor.x = theX;
		aBlock.anchor.y = theY;
		
		aBlock.padding = thePadding;
		aBlock.type = theType;
	}

	void end(int theType) {
		cvui_block_t& aBlock = popBlock();

		if (aBlock.type != theType) {
			error(4, "Calling wrong type of end*(). E.g. endColumn() instead of endRow(). Check if your begin*() calls are matched with their appropriate end*() calls.");
		}

		// If we still have blocks in the stack, we must update
		// the current top with the dimensions that were filled by
		// the newly popped block.

		if (!blockStackEmpty()) {
			cvui_block_t& aTop = topBlock();
			cv::Size aSize(aBlock.fill.width, aBlock.fill.height);

			updateLayoutFlow(aTop, aSize);
		}
	}

	// Find the min and max values of a vector
	void findMinMax(std::vector<double>& theValues, double *theMin, double *theMax) {
		std::vector<double>::size_type aSize = theValues.size(), i;
		double aMin = theValues[0], aMax = theValues[0];

		for (i = 0; i < aSize; i++) {
			if (theValues[i] < aMin) {
				aMin = theValues[i];
			}

			if (theValues[i] > aMax) {
				aMax = theValues[i];
			}
		}

		*theMin = aMin;
		*theMax = aMax;
	}

	void separateChannels(int *theRed, int *theGreen, int *theBlue, unsigned int theColor) {
		*theRed = (theColor >> 16) & 0xff;
		*theGreen = (theColor >> 8) & 0xff;
		*theBlue = theColor & 0xff;
	}

	bool button(cvui_block_t& theBlock, int theX, int theY, int theWidth, int theHeight, const cv::String& theLabel, bool theUpdateLayout) {
		// Calculate the space that the label will fill
		cv::Size aTextSize = getTextSize(theLabel, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, nullptr);

		// Make the button bit enough to house the label
		cv::Rect aRect(theX, theY, theWidth, theHeight);

		// Check the state of the button (idle, pressed, etc.)
		bool aMouseIsOver = aRect.contains(gMouse);

		if (aMouseIsOver) {
			if (gMousePressed) {
				render::button(theBlock, render::PRESSED, aRect, theLabel);
				render::buttonLabel(theBlock, render::PRESSED, aRect, theLabel, aTextSize);
			}
			else {
				render::button(theBlock, render::OVER, aRect, theLabel);
				render::buttonLabel(theBlock, render::OVER, aRect, theLabel, aTextSize);
			}
		}
		else {
			render::button(theBlock, render::IDLE, aRect, theLabel);
			render::buttonLabel(theBlock, render::IDLE, aRect, theLabel, aTextSize);
		}

		// Update the layout flow according to button size
		// if we were told to update.
		if (theUpdateLayout) {
			cv::Size aSize(theWidth, theHeight);
			updateLayoutFlow(theBlock, aSize);
		}

		// Tell if the button was clicked or not
		return aMouseIsOver && gMouseJustReleased;
	}

	bool button(cvui_block_t& theBlock, int theX, int theY, const cv::String& theLabel) {
		// Calculate the space that the label will fill
		cv::Size aTextSize = getTextSize(theLabel, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, nullptr);

		// Create a button based on the size of the text
		return internal::button(theBlock, theX, theY, aTextSize.width + 30, aTextSize.height + 18, theLabel, true);
	}

	bool checkbox(cvui_block_t& theBlock, int theX, int theY, const cv::String& theLabel, bool *theState, unsigned int theColor) {
		int aBaseline = 0;
		cv::Rect aRect(theX, theY, 15, 15);
		cv::Size aTextSize = getTextSize(theLabel, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, &aBaseline);
		cv::Rect aHitArea(theX, theY, aRect.width + aTextSize.width + 6, aRect.height);
		bool aMouseIsOver = aHitArea.contains(gMouse);

		if (aMouseIsOver) {
			render::checkbox(theBlock, render::OVER, aRect);

			if (gMouseJustReleased) {
				*theState = !(*theState);
			}
		}
		else {
			render::checkbox(theBlock, render::IDLE, aRect);
		}

		render::checkboxLabel(theBlock, aRect, theLabel, aTextSize, theColor);

		if (*theState) {
			render::checkboxCheck(theBlock, aRect);
		}

		// Update the layout flow
		cv::Size aSize(aHitArea.width, aHitArea.height);
		updateLayoutFlow(theBlock, aSize);

		return *theState;
	}

	void text(cvui_block_t& theBlock, int theX, int theY, const cv::String& theText, double theFontScale, unsigned int theColor, bool theUpdateLayout) {
		cv::Size aTextSize = cv::getTextSize(theText, cv::FONT_HERSHEY_SIMPLEX, theFontScale, 1, nullptr);
		cv::Point aPos(theX, theY + aTextSize.height);

		render::text(theBlock, theText, aPos, theFontScale, theColor);

		if (theUpdateLayout) {
			cv::Size aTextSize = cv::getTextSize(theText, cv::FONT_HERSHEY_SIMPLEX, theFontScale, 1, nullptr);
			
			// Add an extra pixel to the height to overcome OpenCV font size problems.
			aTextSize.height += 1;
			
			updateLayoutFlow(theBlock, aTextSize);
		}
	}

	int counter(cvui_block_t& theBlock, int theX, int theY, int *theValue, int theStep, const char *theFormat) {
		cv::Rect aContentArea(theX + 22, theY, 48, 22);

		if (internal::button(theBlock, theX, theY, 22, 22, "-", false)) {
			*theValue -= theStep;
		}

		sprintf_s(gBuffer, theFormat, *theValue);
		render::counter(theBlock, aContentArea, gBuffer);

		if (internal::button(theBlock, aContentArea.x + aContentArea.width, theY, 22, 22, "+", false)) {
			*theValue += theStep;
		}

		// Update the layout flow
		cv::Size aSize(22 * 2 + aContentArea.width, 22 * 2 + aContentArea.height);
		updateLayoutFlow(theBlock, aSize);

		return *theValue;
	}

	double counter(cvui_block_t& theBlock, int theX, int theY, double *theValue, double theStep, const char *theFormat) {
		cv::Rect aContentArea(theX + 22, theY, 48, 22);

		if (internal::button(theBlock, theX, theY, 22, 22, "-", false)) {
			*theValue -= theStep;
		}

		sprintf_s(gBuffer, theFormat, *theValue);
		render::counter(theBlock, aContentArea, gBuffer);

		if (internal::button(theBlock, aContentArea.x + aContentArea.width, theY, 22, 22, "+", false)) {
			*theValue += theStep;
		}

		// Update the layout flow
		cv::Size aSize(22 * 2 + aContentArea.width, 22 * 2 + aContentArea.height);
		updateLayoutFlow(theBlock, aSize);

		return *theValue;
	}

	void window(cvui_block_t& theBlock, int theX, int theY, int theWidth, int theHeight, const cv::String& theTitle) {
		cv::Rect aTitleBar(theX, theY, theWidth, 20);
		cv::Rect aContent(theX, theY + aTitleBar.height, theWidth, theHeight - aTitleBar.height);

		render::window(theBlock, aTitleBar, aContent, theTitle);

		// Update the layout flow
		cv::Size aSize(theWidth, theHeight);
		updateLayoutFlow(theBlock, aSize);
	}

	void rect(cvui_block_t& theBlock, int theX, int theY, int theWidth, int theHeight, unsigned int theColor) {
		cv::Rect aRect(theX, theY, theWidth, theHeight);
		render::rect(theBlock, aRect, theColor);

		// Update the layout flow
		cv::Size aSize(aRect.width, aRect.height);
		updateLayoutFlow(theBlock, aSize);
	}

	void sparkline(cvui_block_t& theBlock, std::vector<double> theValues, int theX, int theY, int theWidth, int theHeight, unsigned int theColor) {
		double aMin, aMax;
		cv::Rect aRect(theX, theY, theWidth, theHeight);

		internal::findMinMax(theValues, &aMin, &aMax);
		render::sparkline(theBlock, theValues, aRect, aMin, aMax, theColor);

		// Update the layout flow
		cv::Size aSize(theWidth, theHeight);
		updateLayoutFlow(theBlock, aSize);
	}

	void sparklineChart(cvui_block_t& theBlock, std::vector<double> theValues, int theX, int theY, int theWidth, int theHeight) {
		double aMin, aMax, aScale = 0;

		internal::findMinMax(theValues, &aMin, &aMax);
		aScale = aMax - aMin;

		sparkline(theBlock, theValues, theX, theY, theWidth, theHeight, 0x00FF00);

		sprintf_s(gBuffer, "%.1f", aMax);
		internal::text(theBlock, theX + 2, theY + 8, gBuffer, 0.25, 0x717171, false);

		sprintf_s(gBuffer, "%.1f", aScale / 2 + aMin);
		internal::text(theBlock, theX + 2, theY + theHeight / 2, gBuffer, 0.25, 0x717171, false);

		sprintf_s(gBuffer, "%.1f", aMin);
		internal::text(theBlock, theX + 2, theY + theHeight - 5, gBuffer, 0.25, 0x717171, false);

		// Update the layout flow
		cv::Size aSize(theWidth, theHeight);
		updateLayoutFlow(theBlock, aSize);
	}
}

// This is an internal namespace with all functions
// that actually render each one of the UI components
namespace render {
	void text(cvui_block_t& theBlock, const cv::String& theText, cv::Point& thePos, double theFontScale, unsigned int theColor) {
		int aRed, aGreen, aBlue;

		internal::separateChannels(&aRed, &aGreen, &aBlue, theColor);
		cv::putText(theBlock.where, theText, thePos, cv::FONT_HERSHEY_SIMPLEX, theFontScale, cv::Scalar(aBlue, aGreen, aRed), 1, cv::LINE_AA);
	}

	void button(cvui_block_t& theBlock, int theState, cv::Rect& theShape, const cv::String& theLabel) {
		// Outline
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x29, 0x29, 0x29));

		// Border
		theShape.x++; theShape.y++; theShape.width -= 2; theShape.height -= 2;
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x4A, 0x4A, 0x4A));

		// Inside
		theShape.x++; theShape.y++; theShape.width -= 2; theShape.height -= 2;
		cv::rectangle(theBlock.where, theShape, theState == IDLE ? cv::Scalar(0x42, 0x42, 0x42) : (theState == OVER ? cv::Scalar(0x52, 0x52, 0x52) : cv::Scalar(0x32, 0x32, 0x32)), cv::FILLED);
	}

	void buttonLabel(cvui_block_t& theBlock, int theState, cv::Rect theRect, const cv::String& theLabel, cv::Size& theTextSize) {
		cv::Point aPos(theRect.x + theRect.width / 2 - theTextSize.width / 2, theRect.y + theRect.height / 2 + theTextSize.height / 2);
		cv::putText(theBlock.where, theLabel, aPos, cv::FONT_HERSHEY_SIMPLEX, theState == PRESSED ? 0.39 : 0.4, cv::Scalar(0xCE, 0xCE, 0xCE), 1, cv::LINE_AA);
	}

	void counter(cvui_block_t& theBlock, cv::Rect& theShape, const cv::String& theValue) {
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x29, 0x29, 0x29), cv::FILLED); // fill
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x45, 0x45, 0x45)); // border

		cv::Size aTextSize = getTextSize(theValue, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, nullptr);

		cv::Point aPos(theShape.x + theShape.width / 2 - aTextSize.width / 2, theShape.y + aTextSize.height / 2 + theShape.height / 2);
		cv::putText(theBlock.where, theValue, aPos, cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0xCE, 0xCE, 0xCE), 1, cv::LINE_AA);
	}

	void checkbox(cvui_block_t& theBlock, int theState, cv::Rect& theShape) {
		// Outline
		cv::rectangle(theBlock.where, theShape, theState == IDLE ? cv::Scalar(0x63, 0x63, 0x63) : cv::Scalar(0x80, 0x80, 0x80));

		// Border
		theShape.x++; theShape.y++; theShape.width -= 2; theShape.height -= 2;
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x17, 0x17, 0x17));

		// Inside
		theShape.x++; theShape.y++; theShape.width -= 2; theShape.height -= 2;
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0x29, 0x29, 0x29), cv::FILLED);
	}

	void checkboxLabel(cvui_block_t& theBlock, cv::Rect& theRect, const cv::String& theLabel, cv::Size& theTextSize, unsigned int theColor) {
		cv::Point aPos(theRect.x + theRect.width + 6, theRect.y + theTextSize.height + theRect.height / 2 - theTextSize.height / 2 - 1);
		text(theBlock, theLabel, aPos, 0.4, theColor);
	}

	void checkboxCheck(cvui_block_t& theBlock, cv::Rect& theShape) {
		theShape.x++; theShape.y++; theShape.width -= 2; theShape.height -= 2;
		cv::rectangle(theBlock.where, theShape, cv::Scalar(0xFF, 0xBF, 0x75), cv::FILLED);
	}

	void window(cvui_block_t& theBlock, cv::Rect& theTitleBar, cv::Rect& theContent, const cv::String& theTitle) {
		bool aTransparecy = false;
		double aAlpha = 0.3;
		cv::Mat aOverlay;

		// Render the title bar.
		// First the border
		cv::rectangle(theBlock.where, theTitleBar, cv::Scalar(0x4A, 0x4A, 0x4A));
		// then the inside
		theTitleBar.x++; theTitleBar.y++; theTitleBar.width -= 2; theTitleBar.height -= 2;
		cv::rectangle(theBlock.where, theTitleBar, cv::Scalar(0x21, 0x21, 0x21), cv::FILLED);

		// Render title text.
		cv::Point aPos(theTitleBar.x + 5, theTitleBar.y + 12);
		cv::putText(theBlock.where, theTitle, aPos, cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0xCE, 0xCE, 0xCE), 1, cv::LINE_AA);

		// Render the body.
		// First the border.
		cv::rectangle(theBlock.where, theContent, cv::Scalar(0x4A, 0x4A, 0x4A));

		// Then the filling.
		theContent.x++; theContent.y++; theContent.width -= 2; theContent.height -= 2;
		cv::rectangle(aOverlay, theContent, cv::Scalar(0x31, 0x31, 0x31), cv::FILLED);

		if (aTransparecy) {
			theBlock.where.copyTo(aOverlay);
			cv::rectangle(aOverlay, theContent, cv::Scalar(0x31, 0x31, 0x31), cv::FILLED);
			cv::addWeighted(aOverlay, aAlpha, theBlock.where, 1.0 - aAlpha, 0.0, theBlock.where);

		} else {
			cv::rectangle(theBlock.where, theContent, cv::Scalar(0x31, 0x31, 0x31), cv::FILLED);
		}
	}

	void rect(cvui_block_t& theBlock, cv::Rect& thePos, unsigned int theColor) {
		int aRed, aGreen, aBlue;

		aRed = (theColor >> 16) & 0xff;
		aGreen = (theColor >> 8) & 0xff;
		aBlue = theColor & 0xff;

		cv::rectangle(theBlock.where, thePos, cv::Scalar(aBlue, aGreen, aRed), cv::FILLED, cv::LINE_AA);
	}

	void sparkline(cvui_block_t& theBlock, std::vector<double> theValues, cv::Rect &theRect, double theMin, double theMax, unsigned int theColor) {
		std::vector<double>::size_type aSize = theValues.size(), i;
		double aGap, aPosX, aScale = 0, x, y;
		int aRed, aGreen, aBlue ;

		internal::separateChannels(&aRed, &aGreen, &aBlue, theColor);

		aScale = theMax - theMin;
		aGap = (double)theRect.width / aSize;
		aPosX = theRect.x;

		for (i = 0; i <= aSize - 2; i++) {
			x = aPosX;
			y = (theValues[i] - theMin) / aScale * -(theRect.height - 5) + theRect.y + theRect.height - 5;
			cv::Point aPoint1((int)x, (int)y);

			x = aPosX + aGap;
			y = (theValues[i + 1] - theMin) / aScale * -(theRect.height - 5) + theRect.y + theRect.height - 5;
			cv::Point aPoint2((int)x, (int)y);

			cv::line(theBlock.where, aPoint1, aPoint2, cv::Scalar(aBlue, aGreen, aRed));
			aPosX += aGap;
		}
	}
}
	
void init(const cv::String& theWindowName) {
	cv::setMouseCallback(theWindowName, handleMouse, NULL);
	//TODO: init gScreen here?
}

bool button(cv::Mat& theWhere, int theX, int theY, const cv::String& theLabel) {
	gScreen.where = theWhere;
	return internal::button(gScreen, theX, theY, theLabel);
}

bool button(cv::Mat& theWhere, int theX, int theY, int theWidth, int theHeight, const cv::String& theLabel) {
	gScreen.where = theWhere;
	return internal::button(gScreen, theX, theY, theWidth, theHeight, theLabel, true);
}

bool checkbox(cv::Mat& theWhere, int theX, int theY, const cv::String& theLabel, bool *theState, unsigned int theColor) {
	gScreen.where = theWhere;
	return internal::checkbox(gScreen, theX, theY, theLabel, theState, theColor);
}

void text(cv::Mat& theWhere, int theX, int theY, const cv::String& theText, double theFontScale, unsigned int theColor) {
	gScreen.where = theWhere;
	internal::text(gScreen, theX, theY, theText, theFontScale, theColor, true);
}

void printf(cv::Mat& theWhere, int theX, int theY, double theFontScale, unsigned int theColor, char *theFmt, ...) {
	va_list aArgs;

	va_start(aArgs, theFmt);
	vsprintf_s(gBuffer, theFmt, aArgs);
	va_end(aArgs);	

	gScreen.where = theWhere;
	internal::text(gScreen, theX, theY, gBuffer, theFontScale, theColor, true);
}

void printf(cv::Mat& theWhere, int theX, int theY, char *theFmt, ...) {
	va_list aArgs;

	va_start(aArgs, theFmt);
	vsprintf_s(gBuffer, theFmt, aArgs);
	va_end(aArgs);

	gScreen.where = theWhere;
	internal::text(gScreen, theX, theY, gBuffer, 0.4, 0xCECECE, true);
}

int counter(cv::Mat& theWhere, int theX, int theY, int *theValue, int theStep, const char *theFormat) {
	gScreen.where = theWhere;
	return internal::counter(gScreen, theX, theY, theValue, theStep, theFormat);
}

double counter(cv::Mat& theWhere, int theX, int theY, double *theValue, double theStep, const char *theFormat) {
	gScreen.where = theWhere;
	return internal::counter(gScreen, theX, theY, theValue, theStep, theFormat);
}

void window(cv::Mat& theWhere, int theX, int theY, int theWidth, int theHeight, const cv::String& theTitle) {
	gScreen.where = theWhere;
	internal::window(gScreen, theX, theY, theWidth, theHeight, theTitle);
}

void rect(cv::Mat& theWhere, int theX, int theY, int theWidth, int theHeight, unsigned int theColor) {
	gScreen.where = theWhere;
	internal::rect(gScreen, theX, theY, theWidth, theHeight, theColor);
}

void sparkline(cv::Mat& theWhere, std::vector<double> theValues, int theX, int theY, int theWidth, int theHeight, unsigned int theColor) {
	gScreen.where = theWhere;
	internal::sparkline(gScreen, theValues, theX, theY, theWidth, theHeight, theColor);
}

void sparklineChart(cv::Mat& theWhere, std::vector<double> theValues, int theX, int theY, int theWidth, int theHeight) {
	gScreen.where = theWhere;
	internal::sparkline(gScreen, theValues, theX, theY, theWidth, theHeight, 0x00FF00);
}

void beginRow(cv::Mat &theWhere, int theX, int theY, int theWidth, int theHeight, int thePadding) {
	internal::begin(ROW, theWhere, theX, theY, theWidth, theHeight, thePadding);
}

void endRow() {
	internal::end(ROW);
}

void beginColumn(cv::Mat &theWhere, int theX, int theY, int theWidth, int theHeight, int thePadding) {
	internal::begin(COLUMN, theWhere, theX, theY, theWidth, theHeight, thePadding);
}

void endColumn() {
	internal::end(COLUMN);
}

void beginRow(int theWidth, int theHeight, int thePadding) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::begin(ROW, aBlock.where, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, thePadding);
}

void beginColumn(int theWidth, int theHeight, int thePadding) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::begin(COLUMN, aBlock.where, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, thePadding);
}

bool button(const cv::String& theLabel) {
	cvui_block_t& aBlock = internal::topBlock();
	return internal::button(aBlock, aBlock.anchor.x, aBlock.anchor.y, theLabel);
}

bool button(int theWidth, int theHeight, const cv::String& theLabel) {
	cvui_block_t& aBlock = internal::topBlock();
	return internal::button(aBlock, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, theLabel, true);
}

bool checkbox(const cv::String& theLabel, bool *theState, unsigned int theColor) {
	cvui_block_t& aBlock = internal::topBlock();
	return internal::checkbox(aBlock, aBlock.anchor.x, aBlock.anchor.y, theLabel, theState, theColor);
}

void text(const cv::String& theText, double theFontScale, unsigned int theColor) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::text(aBlock, aBlock.anchor.x, aBlock.anchor.y, theText, theFontScale, theColor, true);
}

void printf(double theFontScale, unsigned int theColor, char *theFmt, ...) {
	cvui_block_t& aBlock = internal::topBlock();
	va_list aArgs;

	va_start(aArgs, theFmt);
	vsprintf_s(gBuffer, theFmt, aArgs);
	va_end(aArgs);

	internal::text(aBlock, aBlock.anchor.x, aBlock.anchor.y, gBuffer, theFontScale, theColor, true);
}

void printf(char *theFmt, ...) {
	cvui_block_t& aBlock = internal::topBlock();
	va_list aArgs;

	va_start(aArgs, theFmt);
	vsprintf_s(gBuffer, theFmt, aArgs);
	va_end(aArgs);

	internal::text(aBlock, aBlock.anchor.x, aBlock.anchor.y, gBuffer, 0.4, 0xCECECE, true);
}

int counter(int *theValue, int theStep, const char *theFormat) {
	cvui_block_t& aBlock = internal::topBlock();
	return internal::counter(aBlock, aBlock.anchor.x, aBlock.anchor.y, theValue, theStep, theFormat);
}

double counter(double *theValue, double theStep, const char *theFormat) {
	cvui_block_t& aBlock = internal::topBlock();
	return internal::counter(aBlock, aBlock.anchor.x, aBlock.anchor.y, theValue, theStep, theFormat);
}

void window(int theWidth, int theHeight, const cv::String& theTitle) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::window(aBlock, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, theTitle);
}

void rect(int theWidth, int theHeight, unsigned int theColor) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::rect(aBlock, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, theColor);
}

void sparkline(std::vector<double> theValues, int theWidth, int theHeight, unsigned int theColor) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::sparkline(aBlock, theValues, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, theColor);
}

void sparklineChart(std::vector<double> theValues, int theWidth, int theHeight) {
	cvui_block_t& aBlock = internal::topBlock();
	internal::sparkline(aBlock, theValues, aBlock.anchor.x, aBlock.anchor.y, theWidth, theHeight, 0x00FF00);
}

void update() {
	gMouseJustReleased = false;

	gScreen.rect.x = 0;
	gScreen.rect.y = 0;
	gScreen.rect.width = 0;
	gScreen.rect.height = 0;
	
	gScreen.fill = gScreen.rect;
	gScreen.fill.width = 0;
	gScreen.fill.height = 0;

	gScreen.anchor.x = 0;
	gScreen.anchor.y = 0;
	
	gScreen.padding = 0;

	if (!internal::blockStackEmpty()) {
		internal::error(2, "Calling update() before finishing all begin*()/end*() calls. Did you forget to call a begin*() or an end*()? Check if every begin*() has an appropriate end*() call before you call update().");
	}
}

void handleMouse(int theEvent, int theX, int theY, int theFlags, void* theData) {
	gMouse.x = theX;
	gMouse.y = theY;

	if (theEvent == cv::EVENT_LBUTTONDOWN || theEvent == cv::EVENT_RBUTTONDOWN)	{
		gMousePressed = true;

	} else if (theEvent == cv::EVENT_LBUTTONUP || theEvent == cv::EVENT_RBUTTONUP)	{
		gMouseJustReleased = true;
		gMousePressed = false;
	}
}

} // namespace cvui