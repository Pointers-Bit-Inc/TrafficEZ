#include "GaussianBlurStep.h"

GaussianBlurStep::GaussianBlurStep(int kernelSize, double sigma)
    : kernelSize(kernelSize)
    , sigma(sigma)
{
    checkGaussianKernelValidity(kernelSize);
}

void GaussianBlurStep::process(cv::Mat& frame) const
{
    cv::GaussianBlur(frame, frame, cv::Size(kernelSize, kernelSize), sigma);
}

void GaussianBlurStep::updateParameters(const StepParameters& newParams)
{
    auto params = std::get_if<GaussianBlurParams>(&newParams.params);
    if(params == nullptr)
    {
        std::cerr << "Please provide a valid GaussianBlurParams, or check if "
                     "you are using the correct builder index.\n";
        return;
    }

    kernelSize = params->kernelSize;
    sigma = params->sigma;
    checkGaussianKernelValidity(kernelSize);
}

/**
 * @brief Checks for the kernel size if it is odd or positive.
 * If it is even, it will add 1. If it is negative, it will set to 1.
 * @param kSize the kernel size to check.
 */
void GaussianBlurStep::checkGaussianKernelValidity(int kSize)
{
    // Check that kernelSize is odd and positive
    if(kSize % 2 == 0 && kSize >= 0)
    {
        std::cerr << "kernelSize must be an odd integer.\n";
        kernelSize += 1;
        std::cerr << "Setting new kernelSize to: " << kernelSize << "\n";
    }

    // Check that kernelSize is positive
    if(kSize < 0)
    {
        std::cerr << "kernelSize must be a positive integer.\n";
        kernelSize = 1;
        std::cerr << "Setting new kernelSize to: " << kernelSize << "\n";
    }
}