from typing import Any
import numpy as np
from PIL import Image
import io
import cv2

"""
RGB565 = "image/rgb565";
GS     = "image/grayscale";
RGB444 = "image/rgb444";
RGB555 = "image/rgb555";
RGB888 = "image/rgb888";
JPEG   = "image/jpeg";
YUV422 = "image/yuv422";
YUV420 = "image/yuv420";
RAW    = "image/raw";
"""

def convert_to_jpeg(image_data, format_type, width, height) -> Any:
    """
    Convert ESP32 image data to JPEG format
    
    Args:
        image_data (bytes): Raw image data from ESP32
        format_type (str): Input image format (e.g., 'image/rgb565')
        width (int): Image width in pixels
        height (int): Image height in pixels
    
    Returns:
        bytes: JPEG encoded image data
    """
    
    def rgb565_to_rgb888(data):
        rgb565 = np.frombuffer(data, dtype=np.uint16).reshape(height, width)
        # Extract channels
        r = ((rgb565 >> 11) & 0x1F)
        g = ((rgb565 >> 5) & 0x3F)
        b = (rgb565 & 0x1F)
        # Convert to RGB888
        r = (r << 3) | (r >> 2)
        g = (g << 2) | (g >> 4)
        b = (b << 3) | (b >> 2)
        # Combine channels
        # bgr565 = (r << 11) | (g << 5) | b
        # bgr565 = bgr565.view(np.uint8).reshape((height, width, 2))
        # rgb888 = cv2.cvtColor(bgr565, cv2.COLOR_BGR5652RGB)
        # Custom np array
        rgb888 = np.stack((r, g, b), axis=-1).astype(np.uint8)
        return rgb888
    
    def rgb444_to_rgb888(data):
        rgb444 = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 2)
        r = (rgb444[:, :, 0] & 0xF0) >> 4
        g = (rgb444[:, :, 0] & 0x0F)
        b = (rgb444[:, :, 1] & 0x0F)
        r = (r << 4) | r  # Scale to 8-bit
        g = (g << 4) | g
        b = (b << 4) | b
        rgb888 = np.stack((r, g, b), axis=-1).astype(np.uint8)
        return rgb888
    
    def rgb555_to_rgb888(data):
        rgb555 = np.frombuffer(data, dtype=np.uint16).reshape(height, width)
        r = (rgb555 & 0x7C00) >> 10
        g = (rgb555 & 0x03E0) >> 5
        b = (rgb555 & 0x001F)
        # Scale from 5-bit to 8-bit
        r = (r << 3) | (r >> 2)
        g = (g << 3) | (g >> 2)
        b = (b << 3) | (b >> 2)
        rgb888 = np.stack((r, g, b), axis=-1).astype(np.uint8)
        return rgb888
    
    def rgb888_to_rgb888(data):
        # Already in RGB888 format, just reshape
        return np.frombuffer(data, dtype=np.uint8).reshape(height, width, 3)
    
    def grayscale_to_rgb888(data):
        # return 2D, handle mode below
        return np.frombuffer(data, dtype=np.uint8).reshape(height, width)
    
    def yuv422_to_rgb888(data):
        yuv422 = np.frombuffer(data, dtype=np.uint8)
        yuv422 = yuv422.reshape(height, width, 2)
        rgb = cv2.cvtColor(yuv422, cv2.COLOR_YUV2RGB_YUYV)
        return rgb
    
    def yuv420_to_rgb888(data):
        yuv422 = np.frombuffer(data, dtype=np.uint8)
        yuv422 = yuv422.reshape(height, width, 2)
        rgb = cv2.cvtColor(yuv422, cv2.COLOR_YUV2RGB_UYVY)
        return rgb

    # Conversion function mapping
    format_converters = {
        'image/rgb565':    rgb565_to_rgb888,
        'image/grayscale': grayscale_to_rgb888,
        'image/rgb444':    rgb444_to_rgb888,
        'image/rgb555':    rgb555_to_rgb888,
        'image/rgb888':    rgb888_to_rgb888,
        'image/yuv422':    yuv422_to_rgb888,
        'image/yuv420':    yuv420_to_rgb888,
    }
    
    try:
        if format_type == 'image/jpeg':
            # If already JPEG, return as is
            return image_data
        elif format_type == 'image/raw':
            # Assume raw data is in RGB888 format
            return rgb888_to_rgb888(image_data)

        if format_type not in format_converters:
            raise ValueError(f"Unsupported image format: {format_type}") 
        
        if format_type == 'image/grayscale':
            arr = np.frombuffer(image_data, dtype=np.uint8).reshape(height, width)
            image = Image.fromarray(arr, mode='L')
        else:
            # Convert to RGB888 format
            rgb_data = format_converters[format_type](image_data)
            # Create PIL Image from numpy array
            image = Image.fromarray(rgb_data)
        
        # Save as JPEG to bytes buffer
        jpeg_buffer = io.BytesIO()
        image.save(jpeg_buffer, format='JPEG')
        
        return jpeg_buffer
    
    except Exception as e:
        raise Exception(f"Error converting image: {str(e)}")
