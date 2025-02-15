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


def img_888_to_jpeg(image_raw, width, height):
    """
    Converts raw RGB888 image data to JPEG format.
    
    Args:
    - image_raw: Raw image data from ESP32 in RGB888 format (24-bit per pixel)
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    array = np.frombuffer(image_raw, dtype=np.uint8).reshape((height, width, 3))
    array = np.fliplr(array)
    img = Image.fromarray(array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_yuv422_to_jpeg(image_raw, width, height):
    """
    Converts raw YUV422 (YUYV) image data to JPEG format.
    
    Args:
    - image_raw: Raw image data from ESP32 in YUV422 format
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    # Reshape into height x width x 2 array (2 bytes per pixel)
    array = np.frombuffer(image_raw, dtype=np.uint8).reshape((height, width, 2))
    array = np.fliplr(array)
    
    # Extract Y, U, V components
    y = array[:, :, 0]
    uv = array[:, :, 1]
    
    # Separate U and V (every other pixel)
    u = uv[:, 0::2]
    v = uv[:, 1::2]
    
    # Upsample U and V to match Y resolution
    u_upsampled = np.repeat(u, 2, axis=1)
    v_upsampled = np.repeat(v, 2, axis=1)
    
    # Convert YUV to RGB
    c = y - 16
    d = u_upsampled - 128
    e = v_upsampled - 128
    
    r = (298 * c + 409 * e + 128) >> 8
    g = (298 * c - 100 * d - 208 * e + 128) >> 8
    b = (298 * c + 516 * d + 128) >> 8
    
    # Clip values to valid range
    r = np.clip(r, 0, 255).astype(np.uint8)
    g = np.clip(g, 0, 255).astype(np.uint8)
    b = np.clip(b, 0, 255).astype(np.uint8)
    
    rgb_array = np.stack((r, g, b), axis=-1)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_yuv420_to_jpeg(image_raw, width, height):
    """
    Converts raw YUV420 (YU12/I420) image data to JPEG format.
    
    Args:
    - image_raw: Raw image data from ESP32 in YUV420 format
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    # Calculate component sizes
    y_size = width * height
    uv_size = y_size // 4  # U and V are quarter resolution
    
    # Extract Y, U, V planes
    y = np.frombuffer(image_raw[:y_size], dtype=np.uint8).reshape((height, width))
    u = np.frombuffer(image_raw[y_size:y_size + uv_size], dtype=np.uint8).reshape((height//2, width//2))
    v = np.frombuffer(image_raw[y_size + uv_size:], dtype=np.uint8).reshape((height//2, width//2))
    
    # Upsample U and V to full resolution
    u_upsampled = np.repeat(np.repeat(u, 2, axis=0), 2, axis=1)
    v_upsampled = np.repeat(np.repeat(v, 2, axis=0), 2, axis=1)
    
    # Convert YUV to RGB
    c = y - 16
    d = u_upsampled - 128
    e = v_upsampled - 128
    
    r = (298 * c + 409 * e + 128) >> 8
    g = (298 * c - 100 * d - 208 * e + 128) >> 8
    b = (298 * c + 516 * d + 128) >> 8
    
    # Clip values to valid range
    r = np.clip(r, 0, 255).astype(np.uint8)
    g = np.clip(g, 0, 255).astype(np.uint8)
    b = np.clip(b, 0, 255).astype(np.uint8)
    
    rgb_array = np.stack((r, g, b), axis=-1)
    rgb_array = np.fliplr(rgb_array)
    img = Image.fromarray(rgb_array, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer


def img_raw_to_jpeg(image_raw, width, height):
    """
    Converts raw Bayer/RAW image data to JPEG format.
    Assumes RGGB Bayer pattern.
    
    Args:
    - image_raw: Raw image data from ESP32 in Bayer format
    - width, height: Width and height of the image
    
    Returns:
    - buffer: A buffer containing the JPEG image. Use buffer.getvalue() to get the binary JPEG data.
    """
    # Reshape raw data into 2D array
    bayer = np.frombuffer(image_raw, dtype=np.uint8).reshape((height, width))
    bayer = np.fliplr(bayer)
    
    # Create empty RGB array
    rgb = np.zeros((height, width, 3), dtype=np.uint8)
    
    # Extract RGGB channels
    r = bayer[0::2, 0::2]  # Red pixels
    g1 = bayer[0::2, 1::2]  # Green pixels on red rows
    g2 = bayer[1::2, 0::2]  # Green pixels on blue rows
    b = bayer[1::2, 1::2]  # Blue pixels
    
    # Simple demosaicing (bilinear interpolation)
    # Red channel
    rgb[0::2, 0::2, 0] = r
    rgb[0::2, 1::2, 0] = (r[:, :-1] + r[:, 1:]) // 2
    rgb[1::2, :, 0] = (rgb[:-1:2, :, 0] + rgb[2::2, :, 0]) // 2
    
    # Green channel (average of G1 and G2)
    rgb[0::2, 1::2, 1] = g1
    rgb[1::2, 0::2, 1] = g2
    rgb[0::2, 0::2, 1] = (g1[:, :-1] + g2[:-1, :]) // 2
    rgb[1::2, 1::2, 1] = (g1[:, 1:] + g2[1:, :]) // 2
    
    # Blue channel
    rgb[1::2, 1::2, 2] = b
    rgb[1::2, 0::2, 2] = (b[:, :-1] + b[:, 1:]) // 2
    rgb[0::2, :, 2] = (rgb[1::2, :, 2] + rgb[3::2, :, 2]) // 2
    
    img = Image.fromarray(rgb, mode='RGB')
    buffer = io.BytesIO()
    img.save(buffer, format="JPEG")
    return buffer
