//
//  DuetXPC.h
//  DuetXPC
//
//  Created by Alexander Shabelnikov on 13.02.2025.
//

#ifndef DuetXPC_h
#define DuetXPC_h

#import <Foundation/Foundation.h>
#import "DuetXPCProtocol.h"

@interface DuetXPC : NSObject <DuetXPCProtocol>

@property (nonatomic, strong, readonly) NSArray<NSXPCConnection *> *clients;

+ (instancetype)sharedService;

@end

#endif /* DuetXPC_h */
