#pragma once

typedef enum MapNotificationType MapNotificationType;

// Tick function for the map notifications
void gclMapNotifications_Tick(void);

// Indicates whether the entity 
bool gclMapNotifications_EntityHasNotification(EntityRef erRef, MapNotificationType eType);