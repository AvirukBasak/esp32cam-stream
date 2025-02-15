import numpy as np
from PIL import Image
import io

def img_565_to_jpeg(image_raw, width, height):
    """
    Args:
    - image_raw: Raw image data from ESP32
    - width, height: Width and height of image as captured
    Returns:
    - buffer: A buffer. Use buffer.getvalue() to get binary JPEG image
    """
    array = np.frombuffer(image_raw, dtype=np.uint16).reshape((height, width))
    array = np.fliplr(array)
    r = ((array & 0xF800) >> 8).astype(np.uint8)
    g = ((array & 0x07E0) >> 3).astype(np.uint8)
    b = ((array & 0x001F) << 3).astype(np.uint8)
    rgb_array = np.stack((r, g, b), axis=-1)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_555_to_jpeg(image_raw, width, height):
    """
    Converts raw RGB555 image data to JPEG format.
    
    Args:
    - image_raw: Raw image data from ESP32 in RGB555 format
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    array = np.frombuffer(image_raw, dtype=np.uint16).reshape((height, width))
    array = np.fliplr(array)
    r = ((array & 0x7C00) >> 7).astype(np.uint8)  # 5 bits for red
    g = ((array & 0x03E0) >> 2).astype(np.uint8)  # 5 bits for green
    b = ((array & 0x001F) << 3).astype(np.uint8)  # 5 bits for blue
    rgb_array = np.stack((r, g, b), axis=-1)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_444_to_jpeg(image_raw, width, height):
    """
    Converts raw RGB444 image data to JPEG format.
    
    Args:
    - image_raw: Raw image data from ESP32 in RGB444 format
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    array = np.frombuffer(image_raw, dtype=np.uint16).reshape((height, width))
    array = np.fliplr(array)
    r = ((array & 0x0F00) >> 4).astype(np.uint8)  # 4 bits for red
    g = ((array & 0x00F0) >> 0).astype(np.uint8)  # 4 bits for green
    b = ((array & 0x000F) << 4).astype(np.uint8)  # 4 bits for blue
    # Scale from 4-bit to 8-bit by multiplying by 17 (0x11)
    r = r * 17
    g = g * 17
    b = b * 17
    rgb_array = np.stack((r, g, b), axis=-1)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_gs_to_jpeg(image_raw, width, height):
    """
    Args:
    - image_raw: Raw image data from ESP32
    - width, height: Width and height of image as captured
    Returns:
    - buffer: A buffer. Use buffer.getvalue() to get binary JPEG image
    """
    array = np.frombuffer(image_raw, dtype=np.uint8).reshape((height, width))
    array = np.fliplr(array)
    img = Image.fromarray(array, mode='L')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer
