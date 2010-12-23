/*
   This program will quickly rescale an image and rotate it as necessary,
   according to the EXIF data embedded (if applicable).

   It will attempt to use the hardware JPEG circuitry of the S3C6410, which
   is possible iff the dimensions are <= 1600x1200.  So the flowchart ends
   up looking like:

   Open EXIF header.  Read the orientation, and whether a thumbnail exists.
        |
        V
    Add EXIF rotation information to rotation argument,
    producing $ROTATE_COUNT
        |
        V
    [Is destination <= 160x120, and does a thumbnail exist?]
       |                                    |
       No                                  Yes
       |                                    |
       V                                    V
   Read JPEG header                 Extract EXIF thumbnail to RAM
       |                                        |
       V                                        |
   [Image src <= 1600x1200?]                    |
       |                |                       V
       No               Yes -> mmap JPG -> Open image using HW JPEG
       |                                        |
       V                                        V
   Exit program with $ROTATE_COUNT+1        Rescale using postprocessor
                                                |
                                                V
                                            Rotate image using HW rotator
                                                |
                                                V
                                            Write JPEG using HW
                                                |
                                                V
                                            Exit 0
*/


